/* vi:ai:tabstop=8:shiftwidth=4:softtabstop=4:expandtab
 *
 * Author: Gabriel Burca (gburca dash fuse at ebixio dot com)
 * Version: 1.3
 * Latest version: http://ebixio.com/rofs-filtered/rofs-filtered.c
 *
 * This FUSE file system allows the user to mount a directory read-only and filter
 * the files shown in the read-only directory based on regular expressions found in
 * the optional /etc/rofs-filtered.rc configuration file. See the rofs-filtered.rc
 * file for more details.
 *
 * What's the use of such a file system? Say you have a ton of *.flac music
 * files, along with the transcoded *.mp3 files in the same directory tree
 * structure. Maybe you want to show only one of the formats to music players
 * that can play both flac and mp3 so that the songs don't show up twice. You
 * might also want to show only mp3 files to players that don't understand the
 * flac format.
 * 
 * Based on:
 * ROFS - The read-only filesystem for FUSE.
 * 
 * On Ubuntu/Debian install: libfuse2, libfuse-dev, fuse-utils
 * Version 2.5 or later of FUSE is required. If needed, it can be obtained from
 * debuntu.org by adding the following line to /etc/apt/sources.list:
 *      deb http://repository.debuntu.org/ dapper multiverse
 *
 * Compile using:
 * make
 * make install
 *
 * Mount by adding the following line to /etc/fstab:
 * /full/path/to/rofs-filtered#/the/read/write/device /the/read/only/mount/point fuse defaults,allow_other 0 0
 *
 * Unmount: fusermount -u /the/read/only/mount/point
 *  OR
 * Unmount: umount /the/read/only/mount/point
 *
 * The user might need to be in the "fuse" UNIX group.
 * 
 * Contributors:
 * Lars Kotthoff <lars@larsko.org>
 * - Added the "-c" command line option
 * - Added the Makefile
 *********************************************************************************
 * Copyright (C) 2006-2007  Gabriel Burca (gburca dash fuse at ebixio dot com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *********************************************************************************
 */

// We depend on version 2.5 of FUSE because it provides an "access" callback.
// Some applications would call "access" and figure out a file is writable
// (which was the default behavior of "access" prior to 2.5), then attempt to
// open the file "rw", fail, and bomb out because of the conflicting info.
#define FUSE_USE_VERSION 25

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <unistd.h>
#include <regex.h>
#include <syslog.h>
#include <stdarg.h>
#include <fuse.h>


// Some hard-coded values:
static const char *EXEC_NAME = "rofs-filtered";
static const int log_facility = LOG_DAEMON;

// Global to store our read-write path
char *rw_path;
char *config_file = "/etc/rofs-filtered.rc";
regex_t **patterns = NULL;
int pattern_count = 0;


/** Translate an rofs path into it's underlying filesystem path */
static char* translate_path(const char* path)
{

    char *rPath= malloc(sizeof(char)*(strlen(path)+strlen(rw_path)+1));
 
    if (!rPath) return NULL;

    strcpy(rPath,rw_path);
    if (rPath[strlen(rPath)-1]=='/') {
        rPath[strlen(rPath)-1]='\0';
    }
    strcat(rPath,path);

    return rPath;
}

/** Log a message to syslog */
static void log_msg(const int level, const char *format, ... /*args*/) {
    va_list ap;
    va_start(ap, format);

    vsyslog(log_facility | level, format, ap);
    va_end(ap);
}

/** Report user-friendly regex errors */
static void log_regex_error(int error, regex_t *regex, const char* pattern) {
    size_t msg_len;
    char *err_msg;

    msg_len = regerror(error, regex, NULL, 0);
    err_msg = (char *)malloc(msg_len);

    if (err_msg) {
        regerror(error, regex, err_msg, msg_len);
        log_msg(LOG_ERR, "RegEx error: \"%s\" while parsing pattern: \"%s\"",
                err_msg, pattern); 
        //printf("Error: %s %s\n", err_msg, pattern);
        free(err_msg);
    }

    regfree(regex);
}

/** Read the RegEx configuration file */
static int read_config(const char *conf_file) {
#define MAX_LINE 1024
    regex_t *regex, *ignore_pattern;
    char line[MAX_LINE];
    int regcomp_res, pcount = 0;
    char *eol = NULL;

    FILE *fh = fopen(conf_file, "r");
    if (fh == NULL) {
        log_msg(LOG_ERR, "Failed to open config file: %s", conf_file);
        return -1;
    }

    ignore_pattern = (regex_t *)malloc(sizeof(regex_t));
    if (! ignore_pattern) {
        log_msg(LOG_ERR, "Out of memory!");
        return -1;
    }
    regcomp_res = regcomp(ignore_pattern, "^#|^\\s*$",
                    REG_EXTENDED | REG_NOSUB);
    if (regcomp_res) {
        log_msg(LOG_ERR, "Failed compiling config parser regex.");
        return -1;
    }

    while ( fgets(line, MAX_LINE, fh) ) {
        if (! regexec(ignore_pattern, line, 0, NULL, 0)) continue;

        regex = (regex_t *)malloc(sizeof(regex_t));
        if (! regex) {
            log_msg(LOG_ERR, "Out of memory!");
            return -1;
        }

        // Remove the \n EOL
        eol = index(line, '\n');
        if (eol) *eol = '\0';
        regcomp_res = regcomp(regex, line, REG_EXTENDED | REG_NOSUB);
        if ( regcomp_res ) {
            log_regex_error(regcomp_res, regex, line);
        } else {
            // Add regex to the stash 
            pcount++;
            patterns = realloc(patterns, sizeof(regex_t *) * pcount);
            patterns[pcount - 1] = regex;
        }
    }

    pattern_count = pcount;

    regfree(ignore_pattern);
    fclose(fh);
    return 0;
}

/** If the file name matches one of the RegEx patterns, hide it. */
static int should_hide(const char *name) {
    int res;
    for (int i = 0; i < pattern_count; i++) {
        res = regexec(patterns[i], name, 0, NULL, 0);
        if (res == 0) {
            // We have a match.
            return 1;
        }
    }
    return 0;
}


/******************************
 *   
 * Callbacks for FUSE
 * 
 ******************************/
 
static int callback_getattr(const char *path, struct stat *st_data) {
    if (should_hide(path)) return -ENOENT;

    int res;
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }
    res = lstat(trpath, st_data);
    free(trpath);
    if(res == -1) {
        return -errno;
    }
    // Remove write permissions = chmod a-w
    st_data->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size) {
    if (should_hide(path)) return -ENOENT;

    int res;
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }
    res = readlink(trpath, buf, size - 1);
    free(trpath);
    if(res == -1) {
        return -errno;
    }
    buf[res] = '\0';
    return 0;
}

static int callback_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
    if (should_hide(path)) return -ENOENT;

    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    dp = opendir(trpath);
    free(trpath);
    if(dp == NULL) {
        return -errno;
    }

    while((de = readdir(dp)) != NULL) {
        if (should_hide(de->d_name)) {
            // hide some files and directories
        } else {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
            if (filler(buf, de->d_name, &st, 0))
                break;
        }
    }

    closedir(dp);
    return 0;
}

static int callback_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void)path;
    (void)mode;
    (void)rdev;
    return -EPERM;
}

static int callback_mkdir(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return -EPERM;
}

static int callback_unlink(const char *path) {
    (void)path;
    return -EPERM;
}

static int callback_rmdir(const char *path) {
    (void)path;
    return -EPERM;
}

static int callback_symlink(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return -EPERM;	
}

static int callback_rename(const char *from, const char *to) {
    if (should_hide(from)) return -ENOENT;

    (void)from;
    (void)to;
    return -EPERM;
}

static int callback_link(const char *from, const char *to) {
    if (should_hide(from)) return -ENOENT;

    (void)from;
    (void)to;
    return -EPERM;
}

static int callback_chmod(const char *path, mode_t mode) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)mode;
    return -EPERM;
}

static int callback_chown(const char *path, uid_t uid, gid_t gid) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)uid;
    (void)gid;
    return -EPERM;
}

static int callback_truncate(const char *path, off_t size) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)size;
    return -EPERM;
}

static int callback_utime(const char *path, struct utimbuf *buf)
{
    (void)path;
    (void)buf;
    return -EPERM;	
}

/** This function should just check if the operation is permitted for the given
 * flags. FUSE will provide it's own file descriptor to the calling
 * application.
 */
static int callback_open(const char *path, struct fuse_file_info *finfo) {
    if (should_hide(path)) return -ENOENT;

    int res;

    /* We allow opens, unless they're tring to write, sneaky
     * people.
     */
    int flags = finfo->flags;

    if ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT) || (flags & O_EXCL) || (flags & O_TRUNC)) {
        return -EPERM;
    }
  	
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }
  
    res = open(trpath, flags);
 
    free(trpath);
    if(res == -1) {
        return -errno;
    }
    close(res);
    return 0;
}

static int callback_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo) {
    if (should_hide(path)) return -ENOENT;

    int fd;
    int res;
    (void)finfo;

    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    errno = 0;
    fd = open(trpath, O_RDONLY);
    free(trpath);
    if (fd == -1) return -errno;

    errno = 0;
    res = pread(fd, buf, size, offset);
    
    if (res == -1) {
        res = -errno;
    }
    close(fd);
    return res;
}

static int callback_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *finfo) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)buf;
    (void)size;
    (void)offset;
    (void)finfo;
    return -EPERM;
}

static int callback_statfs(const char *path, struct statvfs *st_buf) {
    if (should_hide(path)) return -ENOENT;

    int res;
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    res = statvfs(trpath, st_buf);
    free(trpath);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int callback_release(const char *path, struct fuse_file_info *finfo) {
    (void) path;
    (void) finfo;
    return 0;
}

static int callback_fsync(const char *path, int crap, struct fuse_file_info *finfo) {
    (void) path;
    (void) crap;
    (void) finfo;
    return 0;
}

static int callback_access(const char *path, int mode) {
    if (should_hide(path)) return -ENOENT;

    if (mode & W_OK) return -1; // We are ReadOnly

    int res;
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }
  	
    errno = 0;
    res = access(trpath, mode);
    free(trpath);
    if (res == -1 && errno != 0) {
        return -errno;
    }
    return res;
}

/**
 * Set the value of an extended attribute
 */
static int callback_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)name;
    (void)value;
    (void)size;
    (void)flags;
    return -EPERM;
}

/**
 * Get the value of an extended attribute.
 */
static int callback_getxattr(const char *path, const char *name, char *value, size_t size) {
    if (should_hide(path)) return -ENOENT;

    int res;
    
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }
    res = lgetxattr(trpath, name, value, size);
    free(trpath);
    if(res == -1) {
        return -errno;
    }
    return res;
}

/**
 * List the supported extended attributes.
 */
static int callback_listxattr(const char *path, char *list, size_t size) {
    if (should_hide(path)) return -ENOENT;

    int res;
	
    char *trpath=translate_path(path);
	
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    res = llistxattr(trpath, list, size);
    free(trpath);
    if(res == -1) {
        return -errno;
    }
    return res;

}

/**
 * Remove an extended attribute.
 */
static int callback_removexattr(const char *path, const char *name) {
    if (should_hide(path)) return -ENOENT;

    (void)path;
    (void)name;
    return -EPERM;

}

struct fuse_operations callback_oper = {
    .getattr	    = callback_getattr,
    .readlink	    = callback_readlink,
    .readdir	    = callback_readdir,
    .mknod	= callback_mknod,
    .mkdir	= callback_mkdir,
    .symlink	= callback_symlink,
    .unlink	= callback_unlink,
    .rmdir	= callback_rmdir,
    .rename	= callback_rename,
    .link	= callback_link,
    .chmod	= callback_chmod,
    .chown	= callback_chown,
    .truncate	= callback_truncate,
    .utime	= callback_utime,
    .open	    = callback_open,
    .read	    = callback_read,
    .write	= callback_write,
    .statfs	    = callback_statfs,
    .release	= callback_release,
    .fsync	= callback_fsync,
    .access	= callback_access,

    /* Extended attributes support for userland interaction */
    .setxattr	= callback_setxattr,
    .getxattr	    = callback_getxattr,
    .listxattr	    = callback_listxattr,
    .removexattr= callback_removexattr
};


int main(int argc, char *argv[])
{
    openlog(EXEC_NAME, LOG_PID, log_facility);
    
    int c = 0;
    while ((c = getopt (argc, argv, "c:")) != -1) {
        switch (c) {
          case 'c':
            config_file = optarg;
            break;
          case '?':
            if (optopt == 'c')
              fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else
              fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            return 1;
          default:
            abort ();
        }
    }
    for (int i = optind; i < argc; i++) {
        argv[i - (optind - 1)] = argv[i];
    }
    argc -= optind - 1;

    if (argc < 3) {
        fprintf(stderr, "Usage: rofs-filtered [-c config] <RW-Path> <Filtered-Path> [FUSE options]\n");
        log_msg(LOG_ERR, "Not enough arguments. argc = %i", argc);
        exit(1);
    }

    if (access(argv[1], F_OK)) {
        log_msg(LOG_ERR, "The following directory does not exist: %s", argv[1]);
        exit(2);
    }
    if (access(argv[2], F_OK)) {
        log_msg(LOG_ERR, "The following directory does not exist: %s", argv[2]);
        exit(3);
    }

    // We save away the first argument (the RW-Path)
    int len = strlen(argv[1]) + 1;
    rw_path = malloc(len);
    if (rw_path) {
        strncpy(rw_path, argv[1], len);
    } else {
        exit(4);
    }

    // Shift all arguments up by one (overwriting the first argument) because
    // fuse_main doesn't accept the mount source, only the mountpoint.
    for (int i = 2; i < argc; i++) {
        argv[i - 1] = argv[i];
    }
    argc--;

    if (read_config(config_file)) {
        log_msg(LOG_ERR, "Error parsing config file: %s", config_file);
    }

    // Hand off control to FUSE
    fuse_main(argc, argv, &callback_oper);
    return 0;
}

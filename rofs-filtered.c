/* vi:ai:tabstop=8:shiftwidth=4:softtabstop=4:expandtab
 *
 * Author: Gabriel Burca (gburca dash fuse at ebixio dot com)
 * Version: 1.7
 * Latest version:
 *      https://github.com/gburca/rofs-filtered
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
 *********************************************************************************
 * Copyright (C) 2006-2014  Gabriel Burca (gburca dash fuse at ebixio dot com)
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

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if __APPLE__ 
#define llistxattr(path, list, size) (listxattr(path, list, size, XATTR_NOFOLLOW))
#define lgetxattr(path, name, value, size) (getxattr(path, name, value, size, 0, XATTR_NOFOLLOW))
#define lsetxattr(path, name, value, size, flags) (setxattr(path, name, value, size, 0, flags | XATTR_NOFOLLOW))
#endif

// We depend on version 2.5 of FUSE because it provides an "access" callback.
// Some applications would call "access" and figure out a file is writable
// (which was the default behavior of "access" prior to 2.5), then attempt to
// open the file "rw", fail, and bomb out because of the conflicting info.
#define FUSE_USE_VERSION 25

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <strings.h>
#include <unistd.h>

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <regex.h>
#include <syslog.h>
#include <fuse.h>

// AC_HEADER_STDC
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// AC_HEADER_DIRENT
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

// Some hard-coded values for use with syslog
static const char *EXEC_NAME = "rofs-filtered";
static const int log_facility = LOG_DAEMON;

struct rofs_config {
    char *rw_path;
    char *config;
    int invert;
    int debug;
};

// Global to store our configuration (the option parsing results)
struct rofs_config conf;

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_DEBUG,
};


#ifdef SYSCONF_DIR
char *default_config_file = SYSCONF_DIR "/rofs-filtered.rc";
#else
char *default_config_file = "/etc/rofs-filtered.rc";
#endif

regex_t **patterns = NULL;
int pattern_count = 0;
mode_t *modes = NULL;
int modes_count = 0;

/** Log a message to syslog */
static inline void log_msg(const int level, const char *format, ... /*args*/) {
    if (level == LOG_DEBUG && !conf.debug) return;

    va_list ap;

    va_start(ap, format);
    vsyslog(log_facility | level, format, ap);
    va_end(ap);

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static char* concat_path(const char* p1, const char* p2) {
    assert(p1 && p2);

    size_t p1len = strlen(p1);
    size_t p2len = strlen(p2);

    assert(p1len && p2len);

    // Need room for path separator and null terminator
    char *path = malloc(p1len + p2len + 2);
    if (!path) return NULL;

    strcpy(path, p1);

    if ((path[p1len - 1] != '/') && (p2[0] != '/')) {
        // Add a "/" if neither p1 nor p2 has it.
        strcat(path, "/");
    } else if (path[p1len - 1] == '/') {
        // If p1 ends in '/', we don't need it from p2
        while (p2[0] == '/') p2++;
    }

    strcat(path, p2);

    return path;
}

/** Translate an rofs path into its underlying filesystem path.
 *
 * @param path The full path, relative to the rofs mount point. For example, if
 * the rofs is mounted at /a/path and there's a /a/path/file, the 'ls /a/path'
 * command will result in calls to this function with the path argument set to
 * "/" and "/file". */
static char* translate_path(const char* path)
{

    return concat_path(conf.rw_path, path);
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

static int add_mode(mode_t mode) {
    mode &= S_IFMT;
    for (int i = 0; i < modes_count; ++i)
        if (mode == modes[i])
            return 1;
    int n = modes_count + 1;
    mode_t *m = (mode_t*)realloc(modes, n * sizeof(*modes));
    if (!m) {
        log_msg(LOG_ERR, "Out of memory for additional type.");
        return 0;
    }
    modes = m;
    modes[modes_count] = mode;
    modes_count = n;
    return 1;
}

/** Read the RegEx configuration file */
static int read_config(const char *conf_file) {
    char *line = NULL;
    size_t line_size = 0;
    int regcomp_res, pcount = 0;
    int ret = 0;

    FILE *fh = fopen(conf_file, "r");
    if (fh == NULL) {
        log_msg(LOG_ERR, "Failed to open config file: %s", conf_file);
        ret = -1;
        goto exit;
    }

    // File types we want to ignore
    regex_t type_pattern;
    regcomp_res = regcomp(&type_pattern, "^\\|\\s*type:\\s*(CHR|BLK|FIFO|LNK|SOCK)\\s*$", REG_EXTENDED);
    if (regcomp_res) {
        log_msg(LOG_ERR, "Failed compiling config parser regex.");
        ret = -3;
        goto free_fh;
    }

    // Config file lines we want to ignore
    regex_t ignore_pattern;
    regcomp_res = regcomp(&ignore_pattern, "^#|^\\s*$",
                    REG_EXTENDED | REG_NOSUB);
    if (regcomp_res) {
        log_msg(LOG_ERR, "Failed compiling config parser regex.");
        ret = -3;
        goto free_type;
    }

    while ( getline(&line, &line_size, fh) >= 0 ) {
        // Ignore comments or empty lines in the config file
        if (! regexec(&ignore_pattern, line, 0, NULL, 0)) continue;

        // Remove the \n EOL
        char *eol = index(line, '\n');
        if (eol) *eol = '\0';

        // Process types
        regmatch_t match[2];
        if (! regexec(&type_pattern, line, sizeof(match) / sizeof(*match), match, 0)) {
            log_msg(LOG_DEBUG, "Type: %s", line+5);
            if (strncmp(line + match[1].rm_so, "CHR", 3) == 0) {
                if (!add_mode(S_IFCHR)) goto free_patterns;
            } else if (strncmp(line + match[1].rm_so, "BLK", 3) == 0) {
                if (!add_mode(S_IFBLK)) goto free_patterns;
            } else if (strncmp(line + match[1].rm_so, "LNK", 3) == 0) {
                if (!add_mode(S_IFLNK)) goto free_patterns;
            } else if (strncmp(line + match[1].rm_so, "FIFO", 4) == 0) {
                if (!add_mode(S_IFIFO)) goto free_patterns;
            } else if (strncmp(line + match[1].rm_so, "SOCK", 4) == 0) {
                if (!add_mode(S_IFSOCK)) goto free_patterns;
            }
            continue;
        }

        // Record pathname pattern
        regex_t *regex = (regex_t *)malloc(sizeof(*regex));
        if (! regex) {
            log_msg(LOG_ERR, "Out of memory!");
            ret = -4;
            goto free_patterns;
        }

        regcomp_res = regcomp(regex, line, REG_EXTENDED | REG_NOSUB);
        if ( regcomp_res ) {
            log_regex_error(regcomp_res, regex, line);
            free(regex);
        } else {
            log_msg(LOG_DEBUG, "Pattern: %s", line);
            // Add regex to the stash
            pcount++;
            regex_t **more_patterns = realloc(patterns, sizeof(regex_t *) * pcount);
            if (more_patterns) {
                patterns = more_patterns;
                patterns[pcount - 1] = regex;
            } else {
                log_msg(LOG_ERR, "Out of memory!");
                pcount--;
                ret = -5;
                free(regex);
                goto free_patterns;
            }
        }
    }

    pattern_count = pcount;
    ret = 0;
    goto free_norm;

free_patterns:
    while (pcount--) {
        free(patterns[pcount]);
    }
    free(patterns);
    patterns = NULL;
    pattern_count = 0;
    free(modes);
    modes = NULL;
    modes_count = 0;

free_norm:
    free(line);
    regfree(&ignore_pattern);
free_type:
    regfree(&type_pattern);
free_fh:
    fclose(fh);
exit:
    return ret;
}

/** If the file name matches one of the RegEx patterns, hide it. */
static int should_hide(const char *name, mode_t mode) {
    mode &= S_IFMT;
    log_msg(LOG_DEBUG, "should_hide: %s %07o", name, mode);
    for (int i = 0; i < modes_count; ++i)
        if (mode == modes[i]) {
            log_msg(LOG_DEBUG, "type: %07o %s", mode, name);
            return !conf.invert;
         }
    if (conf.invert && mode != S_IFREG && mode != S_IFDIR)
        return conf.invert;
    for (int i = 0; i < pattern_count; i++) {
        int res = regexec(patterns[i], name, 0, NULL, 0);
        if (res == 0) {
            // We have a match.
            log_msg(LOG_DEBUG, "match: %d %s", i+1, name);
            return !conf.invert;
        }
    }
    return conf.invert;
}

/******************************
 *
 * Callbacks for FUSE
 *
 ******************************/

static int callback_getattr(const char *path, struct stat *st_data) {
    char *trpath=translate_path(path);
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    int res = lstat(trpath, st_data);
    free(trpath);

    if (should_hide(path, st_data->st_mode)) return -ENOENT;

    if (res == -1) {
        return -errno;
    }
    // Remove write permissions = chmod a-w
    st_data->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size) {
    if (should_hide(path, S_IFLNK)) return -ENOENT;

    char *trpath=translate_path(path);
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    int res = readlink(trpath, buf, size - 1);
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
    if (should_hide(path, S_IFREG)) return -ENOENT;

    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    char *trpath = translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    dp = opendir(trpath);
    free(trpath);
    if (dp == NULL) {
        return -errno;
    }

    while((de = readdir(dp)) != NULL) {
        char *fullPath = concat_path(path, de->d_name);

        if (!fullPath) {
            closedir(dp);
            errno = ENOMEM;
            return -errno;
        }

        int hide = should_hide(fullPath, DTTOIF(de->d_type));
        free(fullPath);

        if (hide) {
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
    if (should_hide(from, S_IFREG)) return -ENOENT;

    (void)from;
    (void)to;
    return -EPERM;
}

static int callback_link(const char *from, const char *to) {
    if (should_hide(from, S_IFREG)) return -ENOENT;

    (void)from;
    (void)to;
    return -EPERM;
}

static int callback_chmod(const char *path, mode_t mode) {
    if (should_hide(path, S_IFREG)) return -ENOENT;

    (void)path;
    (void)mode;
    return -EPERM;
}

static int callback_chown(const char *path, uid_t uid, gid_t gid) {
    if (should_hide(path, S_IFREG)) return -ENOENT;

    (void)path;
    (void)uid;
    (void)gid;
    return -EPERM;
}

static int callback_truncate(const char *path, off_t size) {
    if (should_hide(path, S_IFREG)) return -ENOENT;

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
 * flags. FUSE will provide its own file descriptor to the calling
 * application.
 */
static int callback_open(const char *path, struct fuse_file_info *finfo) {
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    /* We allow opens, unless they're tring to write, sneaky
     * people.
     */
    int flags = finfo->flags;

    if ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT) || (flags & O_EXCL) || (flags & O_TRUNC)) {
        free(trpath);
        return -EPERM;
    }

    int res = open(trpath, flags);

    free(trpath);
    if(res == -1) {
        return -errno;
    }
    close(res);
    return 0;
}

static int callback_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo) {
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    int fd;
    int res;
    (void)finfo;

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
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }
    free(trpath);

    (void)path;
    (void)buf;
    (void)size;
    (void)offset;
    (void)finfo;
    return -EPERM;
}

static int callback_statfs(const char *path, struct statvfs *st_buf) {
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    int res = statvfs(trpath, st_buf);
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
    char *trpath=translate_path(path);

    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    if (mode & W_OK) {
        free(trpath);
        return -1; // We are ReadOnly
    }

    errno = 0;
    int res = access(trpath, mode);
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
          if (should_hide(path, S_IFREG)) return -ENOENT;

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
    char *trpath=translate_path(path);
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    int res = lgetxattr(trpath, name, value, size);
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
    char *trpath=translate_path(path);
    if (!trpath) {
        errno = ENOMEM;
        return -errno;
    }

    struct stat st;
    if (lstat(trpath, &st)) {
        free(trpath);
        return -errno;
    }

    if (should_hide(path, st.st_mode)) {
        free(trpath);
        return -ENOENT;
    }

    int res = llistxattr(trpath, list, size);
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
    if (should_hide(path, S_IFREG)) return -ENOENT;

    (void)path;
    (void)name;
    return -EPERM;

}

struct fuse_operations callback_oper = {
    .getattr    = callback_getattr,
    .readlink   = callback_readlink,
    .readdir    = callback_readdir,
    .mknod      = callback_mknod,
    .mkdir      = callback_mkdir,
    .symlink    = callback_symlink,
    .unlink     = callback_unlink,
    .rmdir      = callback_rmdir,
    .rename     = callback_rename,
    .link       = callback_link,
    .chmod      = callback_chmod,
    .chown      = callback_chown,
    .truncate   = callback_truncate,
    .utime      = callback_utime,
    .open       = callback_open,
    .read       = callback_read,
    .write      = callback_write,
    .statfs     = callback_statfs,
    .release    = callback_release,
    .fsync      = callback_fsync,
    .access     = callback_access,

    /* Extended attributes support for userland interaction */
    .setxattr   = callback_setxattr,
    .getxattr   = callback_getxattr,
    .listxattr  = callback_listxattr,
    .removexattr= callback_removexattr
};

#define ROFS_OPT(t, p, v) { t, offsetof(struct rofs_config, p), v }

static struct fuse_opt rofs_opts[] = {
    ROFS_OPT("source=%s",       rw_path, 0),
    ROFS_OPT("config=%s",       config, 0),
    ROFS_OPT("-c %s",           config, 0),
    ROFS_OPT("invert",          invert, 1),

    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-d",          KEY_DEBUG),
    FUSE_OPT_KEY("--debug",     KEY_DEBUG),
    FUSE_OPT_END
};

static int rofs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
    switch (key) {
    case KEY_HELP:
        fprintf(stderr, "Usage: %s /mount/point -o source=/some/dir [-o config=/some/config.rc] [options]\n"
                "\n"
                "General options:\n"
                "    -o opt,[opt...]         mount options\n"
                "    -h --help               print help\n"
                "    -V --version            print version\n"
                "\n"
                "rofs-filtered options:\n"
                "    -o source=DIR           directory to mount as read-only and filter\n"
                "    -o config=CONFIG_FILE   config file path (default: %s)\n"
                "    -o invert               the config file specifies files to allow\n"
                "\n"
                , outargs->argv[0], default_config_file);
        // Let fuse print out its help text as well...
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &callback_oper);
        exit(1);

    case KEY_VERSION:
        fprintf(stderr, "%s version: %s\n", EXEC_NAME, PACKAGE_VERSION);
        // Let fuse also print its version
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &callback_oper);
        exit(0);

    case KEY_DEBUG:
        fprintf(stderr, "Enable extra logging\n");
        conf.debug = 1;
        break;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    openlog(EXEC_NAME, LOG_PID, log_facility);
    for (int i = 0; i < argc; i++) log_msg(LOG_DEBUG, "    arg %i = %s", i, argv[i]);

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    memset(&conf, 0, sizeof(conf));
    fuse_opt_parse(&args, &conf, rofs_opts, rofs_opt_proc);

    if (conf.config == NULL) conf.config = default_config_file;

    if (conf.rw_path == NULL) {
        log_msg(LOG_ERR, "%s: A source directory was not provided.", PACKAGE_STRING);
        log_msg(LOG_ERR, "%s: See '%s -h' for usage.", PACKAGE_STRING, argv[0]);
        exit(2);
    }

    if (access(conf.rw_path, F_OK)) {
        log_msg(LOG_ERR, "%s: The following source directory does not exist: %s", PACKAGE_STRING, conf.rw_path);
        exit(2);
    }

    log_msg(LOG_INFO, "%s: Starting up. Using source: %s and config: %s", PACKAGE_STRING, conf.rw_path, conf.config);

    if (read_config(conf.config)) {
        log_msg(LOG_ERR, "%s: Error parsing config file: %s", PACKAGE_STRING, conf.config);
        exit(3);
    }

    return fuse_main(args.argc, args.argv, &callback_oper);
}

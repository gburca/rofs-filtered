/* vi:ai:tabstop=8:shiftwidth=4:softtabstop=4:expandtab
 *
 * Author: Gabriel Burca (gburca dash fuse at ebixio dot com)
 * Version: 1.8
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
 * Copyright (C) 2006-2025  Gabriel Burca (gburca dash fuse at ebixio dot com)
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

#include "scope_guard.h"

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

#include <string>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <unordered_map>

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
    const char *rw_path;
    const char *config;
    int invert;
    int debug;
    int preserve_perms;
};

// Global to store our configuration (the option parsing results)
struct rofs_config conf;

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_DEBUG,
};


#ifdef SYSCONF_DIR
const char *default_config_file = SYSCONF_DIR "/rofs-filtered.rc";
#else
const char *default_config_file = "/etc/rofs-filtered.rc";
#endif

regex_t pattern;
bool hasPattern;
std::unordered_set<mode_t> modes;
std::unordered_multimap<std::string, std::string> extPriority;

/** Log a message to syslog and stderr */
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

/** Translate an rofs path into its underlying filesystem path.
 *
 * @param path The full path, relative to the rofs mount point. For example, if
 * the rofs is mounted at /a/path and there's a /a/path/file, the 'ls /a/path'
 * command will result in calls to this function with the path argument set to
 * "/" and "/file". */
static std::filesystem::path translate_path(const std::filesystem::path &path) {
    return conf.rw_path / path.relative_path();
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

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss (s);
    std::string item;

    while (getline (ss, item, delim)) {
        result.push_back (item);
    }

    return result;
}

/** Read the RegEx configuration file */
static int read_config(const std::filesystem::path &conf_file) {
    int regcomp_res;
    std::stringstream full_pattern;   //< Buffer to store the merged patterns

    // File types we want to ignore
    regex_t type_pattern;
    regcomp_res = regcomp(&type_pattern, "^\\|\\s*type:\\s*(CHR|BLK|FIFO|LNK|SOCK)\\s*$", REG_EXTENDED);
    if (regcomp_res) {
        log_msg(LOG_ERR, "Failed compiling config parser regex.");
        return -3;
    }
    scope_guard free_type_pattern = [&](){ regfree(&type_pattern); };

    // Config file lines we want to ignore
    regex_t ignore_pattern;
    regcomp_res = regcomp(&ignore_pattern, "^#|^\\s*$",
                    REG_EXTENDED | REG_NOSUB);
    if (regcomp_res) {
        log_msg(LOG_ERR, "Failed compiling config parser regex.");
        regfree(&type_pattern);
        return -3;
    }
    scope_guard free_ignore_pattern = [&](){ regfree(&ignore_pattern); };

    std::ifstream input(conf_file);
    if (input.fail()) {
        log_msg(LOG_ERR, "Failed to open config file: %s", conf_file.c_str());
        return -1;
    }
    scope_guard close_file = [&](){ input.close(); };

    std::string line;
    while (std::getline(input, line)) {
        // Ignore comments or empty lines in the config file
        if (line.empty()) continue;
        if (! regexec(&ignore_pattern, line.c_str(), 0, NULL, 0)) continue;

        if (line[line.size() - 1] == '\n') {
            line = line.substr(0, line.size() - 1);
        }

        regmatch_t match[2];
        if (! regexec(&type_pattern, line.c_str(), sizeof(match) / sizeof(*match), match, 0)) {
            log_msg(LOG_DEBUG, "Type: %s", line.c_str() + 5);
            if (strncmp(line.c_str() + match[1].rm_so, "CHR", 3) == 0) {
                modes.emplace(S_IFCHR & S_IFMT);
            } else if (strncmp(line.c_str() + match[1].rm_so, "BLK", 3) == 0) {
                modes.emplace(S_IFBLK & S_IFMT);
            } else if (strncmp(line.c_str() + match[1].rm_so, "LNK", 3) == 0) {
                modes.emplace(S_IFLNK & S_IFMT);
            } else if (strncmp(line.c_str() + match[1].rm_so, "FIFO", 4) == 0) {
                modes.emplace(S_IFIFO & S_IFMT);
            } else if (strncmp(line.c_str() + match[1].rm_so, "SOCK", 4) == 0) {
                modes.emplace(S_IFSOCK & S_IFMT);
            }
            continue;
        }

        static const std::string prefix("|extensionPriority:");
        if (line.find(prefix) == 0) {
            auto extensions = split(line.substr(prefix.size()), ',');
            if (extensions.empty()) continue;

            static const std::string dot(".");
            for (auto it = extensions.crbegin(); it != extensions.crend(); ++it) {
                for (auto it2 = it + 1; it2 != extensions.crend(); ++it2) {
                    log_msg(LOG_DEBUG, "%s overrides %s", it2->c_str(), it->c_str());
                    extPriority.emplace(std::make_pair(dot + *it, dot + *it2));
                }
            }
            continue;
        }

        // Test if standalone regex compiles before concatenating.
        regcomp_res = regcomp(&pattern, line.c_str(), REG_EXTENDED | REG_NOSUB);
        if (regcomp_res) {
            // This one failed, we verbosely ignore it.
            log_regex_error(regcomp_res, &pattern, line.c_str());
        } else {
            regfree(&pattern);
            log_msg(LOG_DEBUG, "Pattern: %s", line.c_str());
            
            // Add regex to the buffer
            if (full_pattern.rdbuf()->in_avail()) {
                full_pattern << "|(" << line << ")";
            } else {
                full_pattern << "(" << line << ")";
            }
        }
    }

    std::string pattern_str = full_pattern.str();
    if (pattern_str.empty() && extPriority.empty() && modes.empty()) {
        log_msg(LOG_ERR, "Config file contains no valid pattern.");
        return -1;
    }

    if (pattern_str.empty()) {
        hasPattern = false;
    } else {
        hasPattern = true;
        regcomp_res = regcomp(&pattern, pattern_str.c_str(), REG_EXTENDED | REG_NOSUB);
        if ( regcomp_res ) {
            log_regex_error(regcomp_res, &pattern, pattern_str.c_str());
            return -1;
        }
    }

    log_msg(LOG_DEBUG, "Full regex: %s", pattern_str.c_str());

    return 0;
}

/** If the file name matches one of the RegEx patterns, hide it. */
static int should_hide(const char *name, mode_t mode) {
    mode &= S_IFMT;
    log_msg(LOG_DEBUG, "should_hide test: %07o %s", mode, name);

    if (!conf.invert && !extPriority.empty()) {
        auto fname = translate_path(name);
        auto ext = fname.extension();
        auto range = extPriority.equal_range(ext.string());
        for (auto it = range.first; it != range.second; ++it) {
            fname.replace_extension(it->second);
            if (std::filesystem::exists(fname)) {
                return true;
            }
        }
    }

    for (const auto &m : modes) {
        if (mode == m) {
            log_msg(LOG_DEBUG, "type: %07o %s", mode, name);
            return !conf.invert;
        }
    }
    if (conf.invert && mode != S_IFREG && mode != S_IFDIR)
        return conf.invert;
    if (hasPattern && !regexec(&pattern, name, 0, NULL, 0)) {
        // We have a match.
        log_msg(LOG_DEBUG, "match: %s", name);
        return !conf.invert;
    }
    return conf.invert;
}

/******************************
 *
 * Callbacks for FUSE
 *
 ******************************/

static int callback_getattr(const char *path, struct stat *st_data) {
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    if (lstat(trpath.c_str(), st_data)) return -errno;

    if (should_hide(path, st_data->st_mode)) return -ENOENT;


    // Remove write permissions = chmod a-w
    if (!conf.preserve_perms) {
      st_data->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    }
    return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size) {
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    if (should_hide(path, S_IFLNK)) return -ENOENT;

    int res = readlink(trpath.c_str(), buf, size - 1);
    if (res == -1) return -errno;

    buf[res] = '\0';
    return 0;
}

static int callback_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
    log_msg(LOG_DEBUG, "%s(%s)", __PRETTY_FUNCTION__, path);
    if (should_hide(path, S_IFREG)) return -ENOENT;

    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    auto trpath = translate_path(path);

    dp = opendir(trpath.c_str());
    if (dp == NULL) return -errno;

    while((de = readdir(dp)) != NULL) {
        auto fullPath = std::filesystem::path(path) / de->d_name;

        int stmode = DTTOIF(de->d_type);

        if (stmode == DT_UNKNOWN) {
            struct stat stdata;
            trpath = translate_path(fullPath);
            if (lstat(trpath.c_str(), &stdata)) {
                log_msg(LOG_ERR, "%s: unexpected lstat() error %d for %s", PACKAGE_STRING, errno, fullPath.c_str());
                stmode = 0;
            } else {
                stmode = stdata.st_mode;
            }
        }

        int hide = should_hide(fullPath.c_str(), stmode);

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
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;
    if (should_hide(path, st.st_mode)) return -ENOENT;

    /* We allow opens, unless they're tring to write, sneaky
     * people.
     */
    int flags = finfo->flags;

    if ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_CREAT) || (flags & O_EXCL) || (flags & O_TRUNC)) {
        return -EPERM;
    }

    int res = open(trpath.c_str(), flags);
    if (res == -1) return -errno;

    close(res);
    return 0;
}

static int callback_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo) {
    (void)finfo;
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;


    errno = 0;
    int fd = open(trpath.c_str(), O_RDONLY);
    if (fd == -1) return -errno;

    errno = 0;
    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;

    close(fd);
    return res;
}

static int callback_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *finfo) {
    (void)buf;
    (void)size;
    (void)offset;
    (void)finfo;

    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;

    return -EPERM;
}

static int callback_statfs(const char *path, struct statvfs *st_buf) {
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;

    int res = statvfs(trpath.c_str(), st_buf);
    if (res == -1) return -errno;

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
    auto trpath = translate_path(path);
    log_msg(LOG_DEBUG, "%s(%s, %s)", __PRETTY_FUNCTION__, path, trpath.c_str());

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;

    if (mode & W_OK) return -1; // We are ReadOnly

    errno = 0;
    int res = access(trpath.c_str(), mode);
    if (res == -1 && errno != 0) return -errno;

    return res;
}

/**
 * Set the value of an extended attribute
 */
static int callback_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
    (void)name;
    (void)value;
    (void)size;
    (void)flags;

    if (should_hide(path, S_IFREG)) return -ENOENT;
    return -EPERM;
}

/**
 * Get the value of an extended attribute.
 */
static int callback_getxattr(const char *path, const char *name, char *value, size_t size) {
    auto trpath = translate_path(path);

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;

    int res = lgetxattr(trpath.c_str(), name, value, size);
    if (res == -1) return -errno;

    return res;
}

/**
 * List the supported extended attributes.
 */
static int callback_listxattr(const char *path, char *list, size_t size) {
    auto trpath = translate_path(path);

    struct stat st;
    if (lstat(trpath.c_str(), &st)) return -errno;

    if (should_hide(path, st.st_mode)) return -ENOENT;

    int res = llistxattr(trpath.c_str(), list, size);
    if(res == -1) return -errno;

    return res;
}

/**
 * Remove an extended attribute.
 */
static int callback_removexattr(const char *path, const char *name) {
    (void)name;

    if (should_hide(path, S_IFREG)) return -ENOENT;

    return -EPERM;
}

// /usr/include/fuse/fuse_compat.h
struct fuse_operations callback_oper = {
    .getattr    = callback_getattr,
    .readlink   = callback_readlink,
    // .getdir
    .mknod      = callback_mknod,
    .mkdir      = callback_mkdir,
    .unlink     = callback_unlink,
    .rmdir      = callback_rmdir,
    .symlink    = callback_symlink,
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
    // .flush
    .release    = callback_release,
    .fsync      = callback_fsync,
    /* Extended attributes support for userland interaction */
    .setxattr   = callback_setxattr,
    .getxattr   = callback_getxattr,
    .listxattr  = callback_listxattr,
    .removexattr= callback_removexattr,

    // .opendir
    .readdir    = callback_readdir,
    // .releasedir
    // .fsyncdir
    // .init
    // .destroy
    .access     = callback_access,
    // .create
    // .ftruncate
    // .fgetattr
};

#define ROFS_OPT(t, p, v) { t, offsetof(struct rofs_config, p), v }

static struct fuse_opt rofs_opts[] = {
    ROFS_OPT("source=%s",       rw_path, 0),
    ROFS_OPT("config=%s",       config, 0),
    ROFS_OPT("-c %s",           config, 0),
    ROFS_OPT("invert",          invert, 1),
    ROFS_OPT("preserve-perms",  preserve_perms, 1),
    ROFS_OPT("debug",           debug, 1),
    // ROFS_OPT("debug-inner",     debug, 1),

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
                "    -o preserve-perms        do not clear write permission\n"
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

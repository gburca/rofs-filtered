/* vi:ai:tabstop=8:shiftwidth=4:softtabstop=4:expandtab
 */

/*
 * 
 * Mount any filesytem, or folder tree read-only, anywhere else while
 * hiding all *.flac files and directories.
 * 
 * Based on:
 * ROFS - The read-only filesystem for FUSE.
 * 
 * Consider this code GPLv2.
 * 
 * Debian: Install libfuse2, libfuse-dev, fuse-utils
 * Compile: gcc -o rofs-flac -Wall -ansi -W -std=c99 -g -ggdb -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -lfuse rofs-flac.c
 * Mount: rofs-flac.mount.sh readwrite_filesystem mount_point
 * OR: (export ROFS_RW_PATH="readwrite_filesystem"; rofs-flac mount_point)
 *
 * Unmount: fusermount -u mount_point
 *
 * The user might need to be in the "fuse" UNIX group.
 * 
 */


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
#include <fuse.h>


// Global to store our read-write path
char *rw_path;
static const char logf[] = "/tmp/rofs.log";


// Translate an rofs path into it's underlying filesystem path
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

static int log1(const char* src, const char* msg) {
    FILE *f;
    int res;

    f = fopen(logf, "a");
    if (!f) {
        res = errno;
        return res;
    }
    fprintf(f, "%s %s\n", src, msg);
    fclose(f);
    return 0;
}

static int should_hide(const char *name) {
    //return 0;
    static const char *flac = ".flac";
    const size_t flen = strlen(flac);
    size_t name_len = strlen(name);

    if (name_len > flen &&
        strcasestr(name + (name_len - flen), ".flac")) {
        return 1;
    }

    return 0;
}



/******************************
 *   
 * Callbacks for FUSE
 * 
 * 
 * 
 ******************************/
 
static int callback_getattr(const char *path, struct stat *st_data)
{
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

static int callback_readlink(const char *path, char *buf, size_t size)
{
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
    log1("readdir: ", path);
    return 0;
}

static int callback_mknod(const char *path, mode_t mode, dev_t rdev)
{
  (void)path;
  (void)mode;
  (void)rdev;
  return -EPERM;
}

static int callback_mkdir(const char *path, mode_t mode)
{
  (void)path;
  (void)mode;
  return -EPERM;
}

static int callback_unlink(const char *path)
{
  (void)path;
  return -EPERM;
}

static int callback_rmdir(const char *path)
{
  (void)path;
  return -EPERM;
}

static int callback_symlink(const char *from, const char *to)
{
  (void)from;
  (void)to;
  return -EPERM;	
}

static int callback_rename(const char *from, const char *to)
{
    if (should_hide(from)) return -ENOENT;

  (void)from;
  (void)to;
  return -EPERM;
}

static int callback_link(const char *from, const char *to)
{
  (void)from;
  (void)to;
  return -EPERM;
}

static int callback_chmod(const char *path, mode_t mode)
{
    if (should_hide(path)) return -ENOENT;

  (void)path;
  (void)mode;
  return -EPERM;
    
}

static int callback_chown(const char *path, uid_t uid, gid_t gid)
{
    if (should_hide(path)) return -ENOENT;

  (void)path;
  (void)uid;
  (void)gid;
  return -EPERM;
}

static int callback_truncate(const char *path, off_t size)
{
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

static int callback_open(const char *path, struct fuse_file_info *finfo)
{
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
    // Why are we closing it after opening it?
    // How do we return the file descriptor?
    // Why is the finfo arg unused?
    //
    // This function should just check if the operation is permitted for the given flags.
    // FUSE will provide it's own file descriptor to the calling application.
    return 0;
}

static int callback_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo)
{
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

static int callback_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *finfo)
{
    if (should_hide(path)) return -ENOENT;

  (void)path;
  (void)buf;
  (void)size;
  (void)offset;
  (void)finfo;
  return -EPERM;
}

static int callback_statfs(const char *path, struct statvfs *st_buf)
{
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

static int callback_release(const char *path, struct fuse_file_info *finfo)
{
  (void) path;
  (void) finfo;
  return 0;
}

static int callback_fsync(const char *path, int crap, struct fuse_file_info *finfo)
{
  (void) path;
  (void) crap;
  (void) finfo;
  return 0;
}

static int callback_access(const char *path, int mode)
{
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

/*
 * Set the value of an extended attribute
 */
static int callback_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    if (should_hide(path)) return -ENOENT;

	(void)path;
	(void)name;
	(void)value;
	(void)size;
	(void)flags;
	return -EPERM;
}

/*
 * Get the value of an extended attribute.
 */
static int callback_getxattr(const char *path, const char *name, char *value, size_t size)
{
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

/*
 * List the supported extended attributes.
 */
static int callback_listxattr(const char *path, char *list, size_t size)
{
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

/*
 * Remove an extended attribute.
 */
static int callback_removexattr(const char *path, const char *name)
{
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
	
    rw_path = getenv("ROFS_RW_PATH");
    if (!rw_path)
    {
        fprintf(stderr, "ROFS_RW_PATH not defined in environment.\n");
        exit(1);
    }


    fuse_main(argc, argv, &callback_oper);
    return 0;
}

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([rofs-filtered], [1.6], [gburca-fuse@ebixio.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([rofs-filtered.c])
AC_CONFIG_HEADER([config.h])

# Do not require README file (we use README.md)
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h strings.h sys/statvfs.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset regcomp getline])

# Checks for libraries.
AC_ARG_WITH(libfuse,
AS_HELP_STRING(--with-libfuse=DIR,
               [use libfuse from DIR/lib (rofs-filtered depends on it)]),
   [ac_cv_use_libfuse="$withval"
   LDFLAGS="$LDFLAGS -L$withval/lib"])

# And Fuse headers.
AC_ARG_WITH(fuseinclude,
AS_HELP_STRING(--with-fuseinclude=DIR,
               [use fuse.h from DIR/fuse]),
   [ac_cv_use_libfuse="$withval"
   CPPFLAGS="$CPPFLAGS -I$withval/fuse"])

AC_CACHE_CHECK([whether to use libfuse],
               ac_cv_use_libfuse, ac_cv_use_libfuse=yes)

if test "$ac_cv_use_libfuse" != no; then
   # fuse requires to use the 64 bit off_t
   CPPFLAGS="$CPPFLAGS -D_FILE_OFFSET_BITS=64"
   AC_CHECK_HEADERS([fuse.h])
   AC_CHECK_LIB(fuse, fuse_mount,
      [LIBS="-lfuse $LIBS"
      AC_DEFINE(HAVE_LIBFUSE, 1,
         [Define to 1 if you have the `fuse' library (-lfuse).])],
      [AC_MSG_ERROR([can not use the `fuse' library!
      * fuse library `libfuse' not found or
      * fuse header `fuse.h' not found
      Please install the library and its header files.
      If you are working with a package based distribution look for
      package with a name ending with `-dev' (the developement packages).
      If you have installed the library and the header files
      but they were not found use `--with-libfuse=DIR'
      when the library is installed in DIR/lib
      and the header files are installed in DIR/include.])])
   AC_MSG_CHECKING([whether libfuse is found when running])
   AC_RUN_IFELSE([
      AC_LANG_PROGRAM([[
#ifdef HAVE_FUSE_H
#include <fuse.h>
#endif]],
      [[return 0;]])],
      AC_MSG_RESULT(yes),
      [AC_MSG_RESULT(no)
      AC_MSG_ERROR([running a program linked with the `fuse' library failed
      * fuse library `libfuse' may not be found by the dynamic linker/loader
      * try to include the path to the fuse library in the environment
        variable LD_LIBRARY_PATH, e.g. LD_LIBRARY_PATH=DIR/lib 
        when the library is installed in DIR/lib])],
      AC_MSG_RESULT([is not necessary]))
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

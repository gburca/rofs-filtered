# Process this file with automake to produce Makefile.in

bin_PROGRAMS = rofs-filtered
rofs_filtered_SOURCES = rofs-filtered.c
#sysconf_DATA = rofs-filtered.rc
dist_sysconf_DATA = rofs-filtered.rc

DEFINE = -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
DEFINE += -DSYSCONF_DIR=${sysconfdir}
DEFINE += -DHAVE_CONFIG_H

AM_CFLAGS = -Wall -std=c99 ${DEFINE}
AM_LDFLAGS = -lfuse


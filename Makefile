CC=gcc
INSTALL=install
CFLAGS=-Wall -std=c99 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse
DESTDIR=/
BINDIR=usr/bin
CONFDIR=etc

EXECUTABLE=rofs-filtered
SRCS=rofs-filtered.c
CONF=rofs-filtered.rc

rofs-filtered:
	$(CC) $(CFLAGS) -o $(EXECUTABLE) $(LDFLAGS) $(SRCS)

all: rofs-filtered

install: all
	mkdir -p ${DESTDIR}${BINDIR}
	$(INSTALL) $(EXECUTABLE) ${DESTDIR}${BINDIR}
	$(INSTALL) -m 644 --backup=numbered $(CONF) ${DESTDIR}${CONFDIR}

clean:
	rm -f *.o $(EXECUTABLE)

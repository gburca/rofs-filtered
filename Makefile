CC=gcc
INSTALL=install
CFLAGS=-Wall -std=c99 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse
DESTDIR=/
BINDIR=usr/bin

EXECUTABLE=rofs-filtered
SRCS=rofs-filtered.c

rofs-filtered:
	$(CC) $(CFLAGS) -o $(EXECUTABLE) $(LDFLAGS) $(SRCS)

all: rofs-filtered

install: all
	mkdir -p ${DESTDIR}${BINDIR}
	$(INSTALL) $(EXECUTABLE) ${DESTDIR}${BINDIR}

clean:
	rm -f *.o $(EXECUTABLE)

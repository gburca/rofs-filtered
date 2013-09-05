This FUSE file system allows the user to mount a directory tree as read-only
and filter the files shown in the read-only directory tree based on regular
expressions found in the rofs-filtered.rc configuration file. See the
rofs-filtered.rc file for more details.

What's the use of such a file system? Say you have a ton of *.flac music
files, along with the transcoded *.mp3 files in the same directory tree
structure. Maybe you want to show only one of the formats to music players
that can play both flac and mp3 so that the songs don't show up twice. You
might also want to show only mp3 files to players that don't understand the
flac format.

Based on:
ROFS - The read-only filesystem for FUSE.

Get the latest version from:
	http://ebixio.com/rofs-filtered/


### Dependencies:
* libfuse2
* libfuse-dev
* fuse-utils
	Version 2.5 or later of FUSE is required.


### Building:
```
./autogen.sh
./configure
make
sudo make install
```

See also the INSTALL file for more build and install instructions.


### Using:

* The rofs-filtered.rc file contains instructions on how to configure the
filtering. This file is installed by default in /usr/local/etc.

* Mount a directory tree by adding a similar line to /etc/fstab: 
```
/full/path/to/rofs-filtered#/the/read/write/device /the/read/only/mount/point fuse defaults,allow_other 0 0
```

* The rofs-filtered executable can also be called directly:
```
rofs-filtered [-c config] <RW-Path> <Filtered-Path> [FUSE options]
```

* To unmount use one of these two commands:
```
fusermount -u /the/read/only/mount/point 
# OR
umount /the/read/only/mount/point
```

* On some systems, the user will need to be in the "fuse" UNIX group.


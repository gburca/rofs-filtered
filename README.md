This FUSE file system allows the user to mount a directory tree as read-only
and filter the files shown in the read-only directory tree based on regular
expressions found in the rofs-filtered.rc configuration file. See the
rofs-filtered.rc file for more details.

What's the use of such a file system? Here are two use cases:

* Say you have a ton of \*.flac music files, along with the transcoded \*.mp3
files in the same directory tree structure. Maybe you want to show only one of
the formats to music players that can play both flac and mp3 so that the songs
don't show up twice. You might also want to show only mp3 files to players that
don't understand the flac format.

* If you take pictures with your DSLR in raw camera format, you might want to
allow an application RO access to only the JPG version of the images.

Based on:
ROFS - The read-only filesystem for FUSE.

Get the latest version from:
	https://github.com/gburca/rofs-filtered


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

On Mac OS X 10.10 Yosemite or later you can use [Homebrew](http://brew.sh/) to install:

    brew install rofs-filtered 


### Using:

```
rofs-filtered --help
```

* The [rofs-filtered.rc](rofs-filtered.rc) file contains instructions on how to configure the
filtering. This file is installed by default in /usr/local/etc.

* Mount a directory tree by adding a similar line to /etc/fstab: 
```
/usr/local/bin/rofs-filtered	/the/read/write/device /the/read/only/mount/point fuse defaults,allow_other 0 0
```

* If you need to have different mount points, each with its own filter rules, you can use an alternative syntax in /etc/fstab:
```
/usr/local/bin/rofs-filtered	/the/ro/mount/point1	fuse	defaults,allow_other,source=/the/rw/device1,config=/etc/filter1.rc
/usr/local/bin/rofs-filtered	/the/ro/mount/point2	fuse	defaults,allow_other,source=/the/rw/device2,config=/etc/filter2.rc
```

* The rofs-filtered executable can also be called directly:
```
rofs-filtered <Filtered-Path> -o source=<RW-Path> [-o config=/etc/filter1.rc] [FUSE options]
```

* To unmount use one of these two commands:
```
fusermount -u /the/read/only/mount/point 
# OR
umount /the/read/only/mount/point
```

* On some systems, the user will need to be in the "fuse" UNIX group.

* The configuration file normally specifies what files should be filtered out.
  When the "invert" option is used, only files that match the RegEx will be
  shown. This can be tricky to configure. See the [rofs-filtered-invert.rc](rofs-filtered-invert.rc) file
  for some tips. The "invert" option can be specified in fstab or on the
  command line respectively as follows:

```
/usr/local/bin/rofs-filtered	/the/ro/mount/point1	fuse	defaults,allow_other,source=/the/rw/device1,config=/etc/filter1.rc,invert
```

```
rofs-filtered <Filtered-Path> -o source=<RW-Path> -o invert [-o config=/etc/filter1.rc] [FUSE options]
```


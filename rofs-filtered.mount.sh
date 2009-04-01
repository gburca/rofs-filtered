#!/bin/bash

function absname () {
	# Returns the absolute filename of a given file or directory.
	if [ -d "$1" ] ; then   # Only a directory name.
		dir="$1"
		unset file
	elif [ -f "$1" ] ; then # Strip off and save the filename.
		dir=$(dirname "$1")
		file="/"$(basename "$1")
	else
		# The file did not exist.
		# Return null string as error.
		echo
		return 1
	fi
 
	# Change to the directory and display the absolute pathname.
	cd "$dir"  > /dev/null
	echo ${PWD}${file}
}

if [ $# -lt 2 ]; then
    echo "Usage: $0 readwrite-directory mount-point" 1>&2
    echo "Usage: fusermount -u mount-point" 1>&2
    exit 1
fi

# Check fuse module
if [ -z "$(grep fuse /proc/modules 2> /dev/null)" ]; then
    if [ $UID = 0 ]; then
        modprobe fuse > /dev/null 2>&1 || {
            echo "Could not load fuse kernel module !" 1>&2
            exit 1
        }
    else
        echo "You need the fuse kernel module to run this. As you are"
        echo "not root, I can't load it. Become root and run :"
        echo
        echo "    modprobe fuse"
        exit 1
    fi
fi

if [ $UID == 0 ]; then
    # Allow other users and check permissions if run by root
    FILESYSTEM_PARAMETERS="${FILESYSTEM_PARAMETERS} -o allow_other"
fi

# Run the daemon
export ROFS_RW_PATH=$(absname "$1")
MOUNTPOINT=$(absname "$2")
eval /usr/local/bin/rofs-filtered "${FILESYSTEM_PARAMETERS}" "$MOUNTPOINT"


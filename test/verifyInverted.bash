#!/bin/bash

cd $(dirname "$0")
. verifyPrelude.bash
"$EXE" $MNT -o source="$PWD"/sourceDir -o config="$SRC"/rofs-filtered-invert.rc -o invert
. verifyPostlude.bash <<EOF
file3.mp3

subDir1:
file3.flac
file3.mp3
fileA.mp3
subSubDir1

subDir1/subSubDir1:

subDir2:
file4.flac
EOF

#!/bin/bash

cd $(dirname "$0")
. verifyPrelude.bash
echo "Finished prelude"
"$EXE" $MNT -o source="$PWD"/sourceDir -o config="$SRC"/rofs-filtered.rc
. verifyPostlude.bash <<EOF
file1.mp3
file2.mp3

subDir1:
file3.mp3
fileA.mp3
subSubDir1

subDir1/subSubDir1:
EOF

#!/bin/bash

cd $(dirname "$0")
. verifyPrelude.bash
"$EXE" $MNT -o source="$PWD"/sourceDir -o config="$SRC"/test/verifyExtensionPriority.rc
. verifyPostlude.bash <<EOF
external-linked.txt
file1.flac
file2.mp3
file3.mp3
image1.raw
image2.jpeg
image3.jpg
type:LNK

extSubDir:
external-linked.txt

subDir1:
file3.flac
fileA.mp3
pipe
socket
subSubDir1

subDir1/subSubDir1:

subDir2:
file4.flac
fileA.mp3
EOF

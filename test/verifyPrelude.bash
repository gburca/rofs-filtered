#!/bin/bash

EXE=rofs-filtered
SRC=/tmp
MNT=/tmp/mnt.$$

eval set -- $(getopt 'x:s:' "$@")
for ((;;)); do
  case "$1" in
    -x)  EXE="$2";  shift 2;;
    -s)  SRC="$2";  shift 2;;
    --) shift; break;;
    *) echo >&2 "Internal error! ($1)"; exit 1;;
  esac
done

# construct the sourceDir according to the reference .rc
mkdir -p sourceDir/{subDir1/subSubDir1,subDir2}
touch sourceDir/{file1.{flac,mp3},file2.mp3,type:LNK}
if [ "$USER" == "root" ]; then
  mknod sourceDir/block b 0 0
  mknod sourceDir/character c 0 0
fi
ln -s subDir1/file3.mp3 sourceDir/
touch sourceDir/subDir1/{file3.{flac,mp3},fileA.mp3}
mknod sourceDir/subDir1/pipe p
nc -lU sourceDir/subDir1/socket &
touch sourceDir/subDir2/{fileA.mp3,file4.{flac,mp3}}

# construct a place to mount the sourceDir
mkdir -p $MNT

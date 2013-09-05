#!/bin/sh

autoreconf --force --install || exit 1;
echo "Now type './configure && make' to compile"

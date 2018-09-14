#!/bin/bash
diff <(cd $MNT && ls -R *) -
ret=$?
fusermount -u $MNT || umount $MNT
nc -U sourceDir/subDir1/socket </dev/null
rm -rf sourceDir $MNT
exit $ret

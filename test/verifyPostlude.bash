#!/bin/bash
diff <(cd $MNT && ls -R *) -
ret=$?
fusermount -u $MNT || umount $MNT
rm -rf sourceDir $MNT
nc -UN sourceDir/subDir1/socket </dev/null
exit $ret

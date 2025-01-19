#!/bin/bash
diff <(cd $MNT && ls -R -1 *) -
ret=$?
if [[ $ret != 0 ]]; then
    echo "Expected:"
    cd $MNT && ls -R -1 -t * && cd -
fi

fusermount -u $MNT || umount $MNT
nc -UN sourceDir/subDir1/socket </dev/null
rm -rf sourceDir sourceDir2 $MNT
exit $ret

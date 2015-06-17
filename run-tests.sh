#!/bin/bash

: ${LSHW:=lshw}
: ${LSHW_ARGS:=-sanitize -numeric -xml}

# We disable network, scsi, and ide because the scans involve a bunch of 
# complicated ioctls which wrapper.so is not capable of capturing and faking 
# (yet).
LSHW_ARGS="-disable network -disable scsi -disable ide $LSHW_ARGS"

if [ ! -f wrapper.so ] ; then
    echo >&2 "** wrapper.so not found, run make"
    exit 1
fi

count=0
for testdir in testdata/* ; do
    ((count++))
    echo >&2 "** Testing $LSHW against $testdir"
    LD_PRELOAD=./wrapper.so LSHW_TEST_DIR=$testdir "$LSHW" $LSHW_ARGS >$testdir/actual.xml
    # strip out informational comments at the top of the output
    sed -e '/<!--.*-->/d' -i $testdir/actual.xml
    diff -u $testdir/expected.xml $testdir/actual.xml
    if [ $? -ne 0 ] ; then
        echo >&2 "** Test failed: differences found in output for $(basename $testdir)"
        exit 1
    fi
done

echo >&2 "** Passed $count tests"

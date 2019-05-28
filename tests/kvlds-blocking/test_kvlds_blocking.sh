#!/bin/sh

# Paths
LBS=../../lbs/lbs
KVLDS=../../kvlds/kvlds
TESTKVLDS=./test_kvlds_blocking
STOR=${KIVALOO_TESTDIR:-`pwd`/stor}
SOCKL=$STOR/sock_lbs
SOCKK=$STOR/sock_kvlds

# Clean up any old tests
rm -rf $STOR

# Start LBS
mkdir $STOR
[ `uname` = "FreeBSD" ] && chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 512 -l 1000000 -1

# Start KVLDS
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024 -1

# Test basic operations
printf "Testing KVLDS blocking operations... "
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Clean up
rm $SOCKK.pid $SOCKK
rm $SOCKL.pid $SOCKL
rm -r $STOR

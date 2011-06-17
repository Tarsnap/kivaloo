#!/bin/sh

# Paths
LBS=../../lbs/lbs
KVLDS=../../kvlds/kvlds
TESTKVLDS=./test_kvlds
STOR=`pwd`/stor
SOCKL=$STOR/sock_lbs
SOCKK=$STOR/sock_kvlds

# Clean up any old tests
rm -rf $STOR

# Start LBS
mkdir $STOR
chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 512 -l 1000000

# 96-byte keys are too big for 512-byte blocks
echo -n "Testing keys too large for the block size... "
if ! $KVLDS -s $SOCKK -l $SOCKL -k 96 -v 32 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
rm $SOCKK

# 200-byte values are too big for 512-byte blocks
echo -n "Testing values too large for the block size... "
if ! $KVLDS -s $SOCKK -l $SOCKL -v 200 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
rm $SOCKK

# Try big requests to a server with a low maximum value length
echo -n "Testing requests with values too large... "
$KVLDS -s $SOCKK -l $SOCKL -v 50
if ! $TESTKVLDS $SOCKK 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
kill `cat $SOCKK.pid`
rm $SOCKK

# Try big requests to a server with a low maximum key length
echo -n "Testing requests with keys too large... "
$KVLDS -s $SOCKK -l $SOCKL -k 5
if ! $TESTKVLDS $SOCKK 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
kill `cat $SOCKK.pid`
rm $SOCKK

# Start KVLDS (the small number of pages should trigger evictions)
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024

# Test basic operations
echo -n "Testing KVLDS operations... "
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Make sure garbage got collected
echo -n "Verifying that old blocks got deleted..."
sleep 1
if [ `ls $STOR/blks_* | wc -l` = 1 ]; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1;
fi

# Make sure a second test works
echo -n "Testing KVLDS connection-closing cleanup..."
if ! $TESTKVLDS $SOCKK; then
	echo " FAILED!"
	exit 1
fi
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Check that an unclean disconnect is handled appropriately
echo -n "Testing KVLDS disconnection cleanup..."
( $TESTKVLDS $SOCKK & ) 2>/dev/null
sleep 0.1 && killall test_kvlds
( $TESTKVLDS $SOCKK & ) 2>/dev/null
sleep 0.1 && killall test_kvlds
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Shut down KVLDS
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK

# Check that killing KVLDS can't break it
echo -n "Testing KVLDS crash-safety..."
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
sleep 0.1 && kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
sleep 0.1 && kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
sleep 0.1 && kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Shut down KVLDS and LBS and clean up
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
kill `cat $SOCKL.pid`
rm $SOCKL.pid $SOCKL
rm -r $STOR

# Make sure we don't leak memory
echo -n "Checking for memory leaks in KVLDS..."
mkdir $STOR
$LBS -s $SOCKL -d $STOR -b 512 -l 1000000 -1
ktrace -i -f ktrace-kvlds.out env MALLOC_OPTIONS=JUV		\
    $KVLDS -s $SOCKK -l $SOCKL -v 104 -1
ktrace -i -f ktrace-test_kvlds.out env MALLOC_OPTIONS=JUV	\
    $TESTKVLDS $SOCKK
sleep 1
rm $SOCKK.pid $SOCKK
rm $SOCKL.pid $SOCKL
rm -r $STOR

# Process ktrace-kvlds output
kdump -Ts -f ktrace-kvlds.out |			\
    grep ' kvlds ' > kdump-kvlds.out
sh ../tools/memleak/memleak.sh kdump-kvlds.out kvlds.leak 2>leak.tmp
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm ktrace-kvlds.out kdump-kvlds.out
	rm kvlds.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in kvlds.leak"
fi
rm leak.tmp

# Process ktrace-test_kvlds output
echo -n "Checking for memory leaks in KVLDS client code..."
kdump -Ts -f ktrace-test_kvlds.out |		\
    grep ' test_kvlds ' > kdump-test_kvlds.out
sh ../tools/memleak/memleak.sh kdump-test_kvlds.out test_kvlds.leak 2>leak.tmp
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm ktrace-test_kvlds.out kdump-test_kvlds.out
	rm test_kvlds.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in test_kvlds.leak"
fi
rm leak.tmp

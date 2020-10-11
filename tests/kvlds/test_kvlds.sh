#!/bin/sh

# Paths
LBS=../../lbs/lbs
KVLDS=../../kvlds/kvlds
MSLEEP=../msleep/msleep
TESTKVLDS=./test_kvlds
STOR=${KIVALOO_TESTDIR:-`pwd`/stor}
SOCKL=$STOR/sock_lbs
SOCKK=$STOR/sock_kvlds

# Clean up any old tests
rm -rf $STOR

# Start LBS
mkdir $STOR
[ `uname` = "FreeBSD" ] && chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 512 -l 1000000

# 96-byte keys are too big for 512-byte blocks
printf "Testing keys too large for the block size... "
if ! $KVLDS -s $SOCKK -l $SOCKL -k 96 -v 32 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
rm $SOCKK

# 200-byte values are too big for 512-byte blocks
printf "Testing values too large for the block size... "
if ! $KVLDS -s $SOCKK -l $SOCKL -v 200 2>/dev/null; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
rm $SOCKK

# Try big requests to a server with a low maximum value length
printf "Testing requests with values too large... "
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
printf "Testing requests with keys too large... "
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
printf "Testing KVLDS operations... "
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Make sure garbage got collected
printf "Verifying that old blocks got deleted..."
# 1 second is not enough to reliably delete all old blocks
sleep 10
if [ `ls $STOR/blks_* | wc -l` = 1 ]; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1;
fi

# Make sure a second test works
printf "Testing KVLDS connection-closing cleanup..."
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
printf "Testing KVLDS disconnection cleanup..."
( $TESTKVLDS $SOCKK & ) 2>/dev/null
$MSLEEP 100 && killall test_kvlds
( $TESTKVLDS $SOCKK & ) 2>/dev/null
$MSLEEP 100 && killall test_kvlds
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
printf "Testing KVLDS crash-safety..."
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
$MSLEEP 100 && kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
$MSLEEP 100 && kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104
$TESTKVLDS $SOCKK 2>/dev/null &
$MSLEEP 100 && kill `cat $SOCKK.pid`
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

# Start LBS with non-power-of-2 byte pages
for X in 1000 1023 1025 1100; do
	mkdir $STOR
	[ `uname` = "FreeBSD" ] && chflags nodump $STOR
	$LBS -s $SOCKL -d $STOR -b ${X} -l 1000000
	$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024
	printf "Testing KVLDS with ${X}-byte pages... "
	if $TESTKVLDS $SOCKK; then
		echo " PASSED!"
	else
		echo " FAILED!"
		exit 1
	fi
	kill `cat $SOCKK.pid`
	rm $SOCKK.pid $SOCKK
	kill `cat $SOCKL.pid`
	rm $SOCKL.pid $SOCKL
	rm -r $STOR
done

# If we're not running on FreeBSD, we can't use utrace and jemalloc to
# check for memory leaks
if ! [ `uname` = "FreeBSD" ]; then
	echo "Can't check for memory leaks on `uname`"
	exit 0
fi

# Make sure we don't leak memory
printf "Checking for memory leaks in KVLDS..."
mkdir $STOR
[ `uname` = "FreeBSD" ] && chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 512 -l 1000000 -1
ktrace -i -f ktrace-kvlds.out env MALLOC_CONF="junk:true,utrace:true"		\
    $KVLDS -s $SOCKK -l $SOCKL -v 104 -1
ktrace -i -f ktrace-test_kvlds.out env MALLOC_CONF="junk:true,utrace:true"	\
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
printf "Checking for memory leaks in KVLDS client code..."
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

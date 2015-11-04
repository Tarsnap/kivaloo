#!/bin/sh

# Paths
LBS=../../lbs/lbs
TESTLBS=./test_lbs
STOR=`pwd`/stor
SOCK=$STOR/sock

# Clean up any old tests
rm -rf $STOR

# Start LBS
mkdir $STOR
chflags nodump $STOR
$LBS -s $SOCK -d $STOR -b 512 -l 1000000

# Perform a first test
echo -n "Testing LBS operations..."
if $TESTLBS $SOCK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Perform a second run
echo -n "Testing LBS connection-closing cleanup..."
if ! $TESTLBS $SOCK; then
	echo " FAILED!"
	exit 1
fi
if $TESTLBS $SOCK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Check that an unclean disconnect is handled appropriately
echo -n "Testing LBS disconnection cleanup..."
( $TESTLBS $SOCK & ) 2>/dev/null
sleep 0.1 && killall test_lbs
if $TESTLBS $SOCK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Check that connections queue appropriately
echo -n "Testing LBS connection queuing..."
$TESTLBS $SOCK &
sleep 0.1
$TESTLBS $SOCK &
sleep 0.1
if $TESTLBS $SOCK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Shut down LBS
kill `cat $SOCK.pid`
rm $SOCK.pid
rm $SOCK
rm -rf $STOR

# Test connecting via different addresses
for S in "localhost:1234" "[127.0.0.1]:1235" "[::1]:1236"; do
	echo -n "Testing LBS with socket at $S..."
	mkdir $STOR
	chflags nodump $STOR
	$LBS -s $S -d $STOR -b 512 -l 1000000 -p $SOCK.pid 2>/dev/null
	if $TESTLBS $S; then
		echo " PASSED!"
	else
		echo " FAILED!"
		exit 1
	fi
	kill `cat $SOCK.pid`
	rm $SOCK.pid
	rm -r $STOR
done

# Make sure we don't leak memory
echo -n "Checking for memory leaks in LBS..."
mkdir $STOR
chflags nodump $STOR
ktrace -i -f ktrace-lbs.out env MALLOC_CONF="junk:true,utrace:true"	\
    $LBS -s $SOCK -d $STOR -b 512 -1
ktrace -i -f ktrace-test_lbs.out env MALLOC_CONF="junk:true,utrace:true"	\
    $TESTLBS $SOCK
sleep 1
rm $SOCK.pid $SOCK
rm -r $STOR

# Process ktrace-lbs output
kdump -Ts -f ktrace-lbs.out |			\
    grep ' lbs ' > kdump-lbs.out
sh ../tools/memleak/memleak.sh kdump-lbs.out lbs.leak 2>leak.tmp
rm ktrace-lbs.out kdump-lbs.out
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm lbs.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in lbs.leak"
fi
rm leak.tmp

# Process ktrace-test_lbs output
echo -n "Checking for memory leaks in LBS client code..."
kdump -Ts -f ktrace-test_lbs.out |		\
    grep ' test_lbs ' > kdump-test_lbs.out
sh ../tools/memleak/memleak.sh kdump-test_lbs.out test_lbs.leak 2>leak.tmp
rm ktrace-test_lbs.out kdump-test_lbs.out
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm test_lbs.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in test_lbs.leak"
fi
rm leak.tmp

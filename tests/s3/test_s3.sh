#!/bin/sh

# Paths
S3=../../s3/s3
TESTS3=./test_s3
TMPDIR=`pwd`/tmp
SOCKS3=$TMPDIR/sock_s3
REGION=s3-us-west-2
BUCKET=kivaloo-test
LOGFILE=s3.log

# Clean up any old tests
rm -rf $TMPDIR

# Start S3 daemon
mkdir $TMPDIR
chflags nodump $TMPDIR
$S3 -s $SOCKS3 -r $REGION -k ~/.s3/aws.key -l $LOGFILE

# Run tests
echo -n "test_s3... "
$TESTS3 $SOCKS3 $BUCKET > test_s3.log
if cmp -s test_s3.log test_s3.good; then
	rm test_s3.log
	echo "PASSED!"
else
	echo "FAILED!"
fi

# Shut down S3 daemon
kill `cat $SOCKS3.pid`
rm $SOCKS3.pid $SOCKS3

# Make sure we don't leak memory
echo -n "Checking for memory leaks in S3 daemon..."
ktrace -i -f ktrace-s3.out env MALLOC_CONF="junk:true,utrace:true"	\
    $S3 -s $SOCKS3 -r $REGION -k ~/.s3/aws.key -l $LOGFILE -1
ktrace -i -f ktrace-test_s3.out env MALLOC_CONF="junk:true,utrace:true"	\
    $TESTS3 $SOCKS3 $BUCKET >/dev/null
sleep 1
rm -r $TMPDIR

# Process ktrace-s3 output
kdump -Ts -f ktrace-s3.out |			\
    grep ' s3 ' > kdump-s3.out
sh ../tools/memleak/memleak.sh kdump-s3.out s3.leak 2>leak.tmp
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm ktrace-s3.out kdump-s3.out
	rm s3.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in s3.leak"
fi
rm leak.tmp

# Process ktrace-test_s3 output
echo -n "Checking for memory leaks in s3 client code..."
kdump -Ts -f ktrace-test_s3.out |		\
    grep ' test_s3 ' > kdump-test_s3.out
sh ../tools/memleak/memleak.sh kdump-test_s3.out test_s3.leak 2>leak.tmp
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm ktrace-test_s3.out kdump-test_s3.out
	rm test_s3.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in test_s3.leak"
fi
rm leak.tmp

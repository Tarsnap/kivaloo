#!/bin/sh

# Paths
S3=../../s3/s3
LBS=../../lbs-s3/lbs-s3
KVLDS=../../kvlds/kvlds
TESTKVLDS=./test_kvlds
TMPDIR=`pwd`/tmp
SOCKS3=$TMPDIR/sock_s3
SOCKL=$TMPDIR/sock_lbs
SOCKK=$TMPDIR/sock_kvlds
REGION=s3-us-west-2
BUCKET=kivaloo-test
LOGFILE=s3.log

# Clean up any old tests
rm -rf $TMPDIR

# Start S3 daemon
mkdir $TMPDIR
chflags nodump $TMPDIR
$S3 -s $SOCKS3 -r $REGION -k ~/.s3/aws.key -l $LOGFILE

# Start LBS
$LBS -s $SOCKL -t $SOCKS3 -b 512 -B $BUCKET

# Start KVLDS (the small number of pages should trigger evictions)
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024

# Test basic operations
echo -n "Testing KVLDS operations against LBS-S3... "
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Test shutting down and restarting KVLDS
echo -n "Testing disconnect and reconnect to LBS-S3... "
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024
if $TESTKVLDS $SOCKK; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK

# Check that killing KVLDS can't break it
echo -n "Testing LBS-S3 tolerance of KVLDS crashes..."
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

# Shut down daemons and and clean up
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
kill `cat $SOCKL.pid`
rm $SOCKL.pid $SOCKL
kill `cat $SOCKS3.pid`
rm $SOCKS3.pid $SOCKS3
rm -r $TMPDIR

# Make sure LBS-S3 doesn't leak memory
echo -n "Checking for memory leaks in LBS-S3..."
mkdir $TMPDIR
chflags nodump $TMPDIR
$S3 -s $SOCKS3 -r $REGION -k ~/.s3/aws.key -l $LOGFILE -1
ktrace -i -f ktrace-lbs-s3.out env MALLOC_CONF="junk:true,utrace:true"		\
    $LBS -s $SOCKL -t $SOCKS3 -b 512 -B $BUCKET -1
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024 -1
$TESTKVLDS $SOCKK
sleep 1
rm $SOCKK.pid $SOCKK
rm $SOCKL.pid $SOCKL
rm $SOCKS3.pid $SOCKS3
rm -r $TMPDIR

# Process ktrace-lbs output
kdump -Ts -f ktrace-lbs-s3.out |			\
    grep ' lbs-s3 ' > kdump-lbs-s3.out
sh ../tools/memleak/memleak.sh kdump-lbs-s3.out lbs-s3.leak 2>leak.tmp
if grep -q 'leaked 0 bytes' leak.tmp; then
	echo " PASSED!"
	rm ktrace-lbs-s3.out kdump-lbs-s3.out
	rm lbs-s3.leak
else
	cat leak.tmp | tr -d '\n'
	echo && echo "  -> memory leaks shown in lbs-s3.leak"
fi
rm leak.tmp

# Launch the stack again and wait until garbage collection is finished
mkdir $TMPDIR
chflags nodump $TMPDIR
$S3 -s $SOCKS3 -r $REGION -k ~/.s3/aws.key -l $LOGFILE
$LBS -s $SOCKL -t $SOCKS3 -b 512 -B $BUCKET
$KVLDS -s $SOCKK -l $SOCKL -v 104 -C 1024
while true; do
	touch $TMPDIR/marker
	sleep 1
	if [ $LOGFILE -ot $TMPDIR/marker ]; then
		break;
	fi
done
rm $TMPDIR/marker
kill `cat $SOCKK.pid`
rm $SOCKK.pid $SOCKK
kill `cat $SOCKL.pid`
rm $SOCKL.pid $SOCKL
kill `cat $SOCKS3.pid`
rm $SOCKS3.pid $SOCKS3
rmdir $TMPDIR

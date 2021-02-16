#!/bin/sh

# Paths
DDBKV=../../dynamodb-kv/dynamodb-kv
LBS=../../lbs-dynamodb/lbs-dynamodb
KVLDS=../../kvlds/kvlds
TESTKVLDS=./test_kvlds
TMPDIR=`pwd`/tmp
SOCKDDBKV=$TMPDIR/sock_ddbkv
SOCKDDBKVM=$TMPDIR/sock_ddbkv_m
SOCKL=$TMPDIR/sock_lbs
SOCKK=$TMPDIR/sock_kvlds
REGION=${REGION:-us-east-1}
TABLE=${TABLE:-kivaloo-testing}
TABLEM=${TABLEM:-kivaloo-testing-m}
NUM_PAIRS=${NUM_PAIRS:-40000}
LOGFILE=dynamodb-kv.log
LOGFILEM=dynamodb-kv-m.log
AWSKEY=~/.dynamodb/aws.key

# If you don't have my AWS keys, you can't run this test.
if ! [ -f $AWSKEY ]; then
	echo "Can't run DynamoDB tests without AWS keys"
	exit 0
fi

# Clean up any old tests
rm -rf $TMPDIR

# Start DynamoDB-KV daemons
mkdir $TMPDIR
$DDBKV -1 -s $SOCKDDBKV -r $REGION -t $TABLE -k $AWSKEY -l $LOGFILE
$DDBKV -1 -s $SOCKDDBKVM -r $REGION -t $TABLEM -k $AWSKEY -l $LOGFILEM

# Start LBS (the small block size should have more nodes being paged in/out)
$LBS -1 -s $SOCKL -t $SOCKDDBKV -m $SOCKDDBKVM -b 512

# Start KVLDS (the small number of pages should trigger evictions)
$KVLDS -s $SOCKK -l $SOCKL -v 104 -k 40 -C 1024

# Test basic operations
printf "Testing KVLDS operations against LBS-DDBKV... "
if $TESTKVLDS $SOCKK $NUM_PAIRS; then
	echo " PASSED!"
else
	echo " FAILED!"
	exit 1
fi

# Let kvlds delete old pages before we shut down
while find ${LOGFILE} -mtime -5s | grep -q .; do
	sleep 1
done

# Shut down kvlds; other daemons should shut down automatically
kill `cat $SOCKK.pid`

# Clean up
rm -r $TMPDIR

#!/bin/sh -e

# Paths
TMPDIR=`pwd`/tmp
REGION=${REGION:-us-east-1}
TABLE=kivaloo-testing
AWSKEY=~/.dynamodb/aws.key
DYNAMODBKV=../../dynamodb-kv/dynamodb-kv
DDBKVLOG=$TMPDIR/dynamodb-kv.log

# If you don't have my AWS keys, you can't run this test.
if ! [ -f $AWSKEY ]; then
        echo "Can't run DynamoDB tests without AWS keys"
        exit 0
fi

# Clean up any old tests
if [ -f $TMPDIR/sock_dynamodb_kv.pid ]; then
	kill `cat $TMPDIR/sock_dynamodb_kv.pid` || true
fi
rm -rf $TMPDIR

# Launch the daemon
mkdir $TMPDIR
$DYNAMODBKV -s $TMPDIR/sock_dynamodb_kv -r $REGION -t $TABLE -k $AWSKEY -l $DDBKVLOG

# Run the test
./test_dynamodb_kv $TMPDIR/sock_dynamodb_kv

# Clean up
kill `cat $TMPDIR/sock_dynamodb_kv.pid`
rm -r $TMPDIR

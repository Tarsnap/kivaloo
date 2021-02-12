#!/bin/sh

# Paths
TMPDIR=`pwd`/tmp
REGION=${REGION:-us-east-1}
TABLE=kivaloo-testing
AWSKEY=~/.dynamodb/aws.key

# If you don't have my AWS keys, you can't run this test.
if ! [ -f $AWSKEY ]; then
	echo "Can't run DynamoDB tests without AWS keys"
	exit 0
fi

# Clean up any old tests
rm -rf $TMPDIR

# Generate signed request
mkdir $TMPDIR
./dynamodb_sign ${AWSKEY} ${REGION} ${TABLE} > ${TMPDIR}/request || exit 1
echo "Signed request:"
echo ">>>>>>>>"
cat ${TMPDIR}/request
echo
echo "<<<<<<<<"

# Send request
nc dynamodb.${REGION}.amazonaws.com 80 < ${TMPDIR}/request > ${TMPDIR}/response
echo "Response from AWS:"
echo ">>>>>>>>"
cat ${TMPDIR}/response
echo
echo "<<<<<<<<"

# Clean up
rm -r $TMPDIR

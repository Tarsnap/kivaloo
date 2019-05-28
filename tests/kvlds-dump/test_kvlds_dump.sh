#!/bin/sh -e

# Paths
LBS=../../lbs/lbs
KVLDS=../../kvlds/kvlds
DUMP=../../kvlds-dump/kvlds-dump
UNDUMP=../../kvlds-undump/kvlds-undump
STOR=`pwd`/stor
SOCKL=$STOR/sock_lbs
SOCKK=$STOR/sock_kvlds
WRKDIR=`pwd`/work

# Clean up any old tests
rm -rf $STOR
rm -rf $WRKDIR

# Start LBS
mkdir $STOR
[ `uname` = "FreeBSD" ] && chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 1024 -1

# Start KVLDS
$KVLDS -s $SOCKK -l $SOCKL

# Create a tree with some key-value pairs
mkdir $WRKDIR $WRKDIR/input
for X in 1 2 3 4 5; do
	mkdir $WRKDIR/input/$X
	echo -n $X > $WRKDIR/input/$X/k
	echo -n "hello world $X" > $WRKDIR/input/$X/v
done

# Load keys
$UNDUMP -t $SOCKK --fs $WRKDIR/input

# Dump them
mkdir $WRKDIR/output1
$DUMP -t $SOCKK --fs $WRKDIR/output1

# Compare input and output1
echo $WRKDIR/input/*/* |
    tr ' ' '\n' |
    sort |
    xargs cat > $WRKDIR/keys-input
echo $WRKDIR/output1/*/* |
    tr ' ' '\n' |
    sort |
    xargs cat > $WRKDIR/keys-output1
cmp $WRKDIR/keys-input $WRKDIR/keys-output1

# Dump keys to a single file
$DUMP -t $SOCKK > $WRKDIR/kvpairs

# Shut down kvlds and wait for lbs to die; then restart them cleanly
kill `cat $SOCKK.pid`
sleep 1;
rm -r $STOR
mkdir $STOR
[ `uname` = "FreeBSD" ] && chflags nodump $STOR
$LBS -s $SOCKL -d $STOR -b 1024 -1
$KVLDS -s $SOCKK -l $SOCKL

# Load keys
$UNDUMP -t $SOCKK < $WRKDIR/kvpairs

# Dump them out again in a new location
mkdir $WRKDIR/output2
$DUMP -t $SOCKK --fs $WRKDIR/output2

# Make sure they're still the same
echo $WRKDIR/output2/*/* |
    tr ' ' '\n' |
    sort |
    xargs cat > $WRKDIR/keys-output2
cmp $WRKDIR/keys-input $WRKDIR/keys-output2

# Shut down kvlds and clean up
kill `cat $SOCKK.pid`
rm -r $STOR
rm -r $WRKDIR

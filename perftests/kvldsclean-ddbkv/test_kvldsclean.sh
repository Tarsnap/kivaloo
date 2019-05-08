#!/bin/sh

set -e

rm -rf stor
mkdir stor
[ `uname` = "FreeBSD" ] && chflags nodump stor
../../dynamodb-kv/dynamodb-kv -1 -s `pwd`/stor/sock_ddbkv -r us-east-1 -t kivaloo-testing2 -k ~/.dynamodb/aws.key -l dynamodb-kv.log
../../lbs-dynamodb/lbs-dynamodb -1 -s `pwd`/stor/sock_lbs -t `pwd`/stor/sock_ddbkv -b 2048
../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs -S 1000
./test_kvldsclean `pwd`/stor/sock_kvlds
while find dynamodb-kv.log -mtime -30s | grep -q .; do
        sleep 1
done
kill `cat stor/sock_kvlds.pid`
rm -r stor

echo "DATE       TIME |WRITES|READS |DELETE"
cat dynamodb-kv.log |
    fgrep '|200|' |
    fgrep 'Item|0' |
    cut -f 1,2 -d '|' |
    perl -pe 's/:..\|/\,/' |
    perl -e 'while (<>) {
		chomp;
		@_ = split /,/;
		$T{$_[0]}{$_[1]} += 1
	};
	for $k (keys %T) {
		printf "%s|%06d|%06d|%06d\n", $k, $T{$k}{"PutItem"},
		    $T{$k}{"GetItem"}, $T{$k}{"DeleteItem"}
	}' |
    sort

#!/bin/sh

set -e

LIST=$( awk -v E=330 'BEGIN {for (i = 0; i < E; i++) print i}')

rm -rf stor
mkdir stor
[ `uname` = "FreeBSD" ] && chflags nodump stor
../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048
../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs -S 1000
( ./test_kvldsclean `pwd`/stor/sock_kvlds &	\
	for X in $LIST; do			\
		du stor 2>/dev/null || true;	\
		sleep 1;			\
	done )
kill `cat stor/sock_kvlds.pid`
kill `cat stor/sock_lbs.pid`
rm -f stor/sock*
rm -r stor

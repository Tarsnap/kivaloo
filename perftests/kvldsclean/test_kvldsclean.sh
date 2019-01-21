#!/bin/sh

set -e

rm -rf stor
mkdir stor
chflags nodump stor
../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048
../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs -S 1000
( ./test_kvldsclean `pwd`/stor/sock_kvlds &	\
	jot 330 | while read X; do		\
		du stor 2>/dev/null || true;	\
		sleep 1;			\
	done )
kill `cat stor/sock_kvlds.pid`
kill `cat stor/sock_lbs.pid`
rm -f stor/sock*
rm -r stor

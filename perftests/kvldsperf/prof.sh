#!/bin/sh

set -e

# Do we want to test MUX?
if [ -n "${WITHMUX+set}" ]; then
	TARGETSOCK=sock_mux
else
	TARGETSOCK=sock_kvlds
	SINGLE=-1
fi

# Do we want to avoid using fsync ("data-loss mode")?
if [ -n "${WITHMUX+set}" ]; then
	DASHL=-L
else
	DASHL=
fi

rm -rf stor
mkdir stor
../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${SINGLE} ${DASHL}
../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs ${SINGLE}
if [ -n "${WITHMUX+set}" ]; then
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
fi
/usr/bin/time -p ./test_kvldsperf `pwd`/stor/${TARGETSOCK}	\
	2>&1 | tr "\n" "\t" ; printf "\n"
if [ -n "${WITHMUX+set}" ]; then
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	./test_kvldsperf `pwd`/stor/${TARGETSOCK} 2>/dev/null || true
	if [ -f mux.gmon ]; then gprof ../../mux/mux mux.gmon > mux.gprof; fi
else
	sleep 5
	if [ -f kvlds.gmon ]; then gprof ../../kvlds/kvlds kvlds.gmon > kvlds-ADD.gprof; fi
fi
rm -f stor/sock*
../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${SINGLE} ${DASHL}
../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs ${SINGLE}
if [ -n "${WITHMUX+set}" ]; then
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
fi
/usr/bin/time -p ./test_kvldsperf `pwd`/stor/${TARGETSOCK}	\
	2>&1 | tr "\n" "\t" ; printf "\n"
if [ -n "${WITHMUX+set}" ]; then
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	./test_kvldsperf `pwd`/stor/${TARGETSOCK} 2>/dev/null || true
else
	sleep 5
	if [ -f kvlds.gmon ]; then gprof ../../kvlds/kvlds kvlds.gmon > kvlds-MODIFY.gprof; fi
fi
rm -r stor

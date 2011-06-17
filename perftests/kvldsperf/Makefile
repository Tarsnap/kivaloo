PROG=	test_kvldsperf
SRCS=	main.c

# Data structures
.PATH.c	:	../../lib/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
SRCS	+=	kvldskey.c
CFLAGS	+=	-I ../../lib/datastruct

# Utility functions
.PATH.c	:	../../lib/util
SRCS	+=	monoclock.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
CFLAGS	+=	-I ../../lib/util

# Fundamental algorithms
.PATH.c	:	../../lib/alg
SRCS	+=	crc32c.c
CFLAGS	+=	-I ../../lib/alg

# Event loop
.PATH.c	:	../../lib/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
CFLAGS	+=	-I ../../lib/events

# Event-driven networking
.PATH.c	:	../../lib/network
SRCS	+=	network_buf.c
CFLAGS	+=	-I ../../lib/network

# Buffered networking
.PATH.c	:	../../lib/netbuf
SRCS	+=	netbuf_read.c
SRCS	+=	netbuf_write.c
CFLAGS	+=	-I ../../lib/netbuf

# Wire protocol
.PATH.c	:	../../lib/wire
SRCS	+=	wire_packet.c
SRCS	+=	wire_readpacket.c
SRCS	+=	wire_writepacket.c
SRCS	+=	wire_requestqueue.c
CFLAGS	+=	-I ../../lib/wire

# KVLDS request/response packets
.PATH.c	:	../../lib/proto_kvlds
SRCS	+=	proto_kvlds_client.c
CFLAGS	+=	-I ../../lib/proto_kvlds

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.ifdef WITHMUX
TARGETSOCK=sock_mux
.else
TARGETSOCK=sock_kvlds
SINGLE=-1
.endif

.ifdef NODISK
DASHL=-L
.else
DASHL=
.endif

test:	all
	rm -rf stor
	mkdir stor
	chflags nodump stor
	../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${DASHL}
	../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs
.ifdef WITHMUX
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
.endif
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
.ifdef WITHMUX
	kill `cat stor/sock_mux.pid`
.endif
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	rm -f stor/sock*
	../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${DASHL}
	../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs
.ifdef WITHMUX
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
.endif
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
.ifdef WITHMUX
	kill `cat stor/sock_mux.pid`
.endif
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	rm -f stor/sock*
	../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${DASHL}
	../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs
.ifdef WITHMUX
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
.endif
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
.ifdef WITHMUX
	kill `cat stor/sock_mux.pid`
.endif
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	rm -f stor/sock*
	rm -r stor

bench:	all
	jot 10 |			\
	    while read X; do		\
		make NODISK=YES test 2>&1;		\
	done |				\
	    grep real |			\
	    awk '{print $$1}' |		\
	    lam - - - - - - |		\
	    tr 's' ' ' |		\
	    rs -T |			\
	    perl -pe 'chomp;		\
		@_ = sort {$$a <=> $$b} split;	\
		$$_ = join " ", @_, "\n";' | 	\
	    rs -T

prof:	all
	rm -rf stor
	mkdir stor
	../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${SINGLE} ${DASHL}
	../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs ${SINGLE}
.ifdef WITHMUX
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
.endif
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
.ifdef WITHMUX
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	./test_kvldsperf `pwd`/stor/${TARGETSOCK} 2>/dev/null || true
	if [ -f mux.gmon ]; then gprof ../../mux/mux mux.gmon > mux.gprof; fi 
.else
	sleep 5
	if [ -f kvlds.gmon ]; then gprof ../../kvlds/kvlds kvlds.gmon > kvlds-ADD.gprof; fi 
.endif
	rm -f stor/sock*
	../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048 ${SINGLE} ${DASHL}
	../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs ${SINGLE}
.ifdef WITHMUX
	../../mux/mux -t `pwd`/stor/sock_kvlds -s `pwd`/stor/sock_mux
.endif
	/usr/bin/time -h ./test_kvldsperf `pwd`/stor/${TARGETSOCK}
.ifdef WITHMUX
	kill `cat stor/sock_kvlds.pid`
	kill `cat stor/sock_lbs.pid`
	./test_kvldsperf `pwd`/stor/${TARGETSOCK} 2>/dev/null || true
.else
	sleep 5
	if [ -f kvlds.gmon ]; then gprof ../../kvlds/kvlds kvlds.gmon > kvlds-MODIFY.gprof; fi 
.endif
	rm -r stor

CLEANFILES+=	kvlds.gmon kvlds-ADD.gprof kvlds-MODIFY.gprof
CLEANFILES+=	mux.gmon mux.gprof

.include <bsd.prog.mk>

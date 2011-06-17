PROG=	test_kvldsclean
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

# LBS request/response packets
.PATH.c	:	../../lib/proto_kvlds
SRCS	+=	proto_kvlds_client.c
CFLAGS	+=	-I ../../lib/proto_kvlds

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

test:	all
	@rm -rf stor
	@mkdir stor
	@chflags nodump stor
	@../../lbs/lbs -s `pwd`/stor/sock_lbs -d stor -b 2048
	@../../kvlds/kvlds -s `pwd`/stor/sock_kvlds -l `pwd`/stor/sock_lbs -S 1000
	@( ./test_kvldsclean `pwd`/stor/sock_kvlds & \
	jot 330 | while read X; do		\
		du stor 2>/dev/null || true;	\
		sleep 1;			\
	done )
	@kill `cat stor/sock_kvlds.pid`
	@kill `cat stor/sock_lbs.pid`
	@rm -f stor/sock*
	@rm -r stor

.include <bsd.prog.mk>

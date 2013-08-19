PROG=	test_kvldsclean
SRCS=	main.c

# Data structures (libcperciva)
.PATH.c	:	../../libcperciva/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
CFLAGS	+=	-I ../../libcperciva/datastruct

# Data structures
.PATH.c	:	../../lib/datastruct
SRCS	+=	kvldskey.c
CFLAGS	+=	-I ../../lib/datastruct

# Utility functions
.PATH.c	:	../../libcperciva/util
SRCS	+=	monoclock.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
CFLAGS	+=	-I ../../libcperciva/util

# Fundamental algorithms
.PATH.c	:	../../libcperciva/alg
SRCS	+=	crc32c.c
CFLAGS	+=	-I ../../libcperciva/alg

# Event loop
.PATH.c	:	../../libcperciva/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_network_selectstats.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
CFLAGS	+=	-I ../../libcperciva/events

# Event-driven networking
.PATH.c	:	../../libcperciva/network
SRCS	+=	network_buf.c
CFLAGS	+=	-I ../../libcperciva/network

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

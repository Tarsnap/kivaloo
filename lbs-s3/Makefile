# Program name
PROG	=	lbs-s3

# Library code required
LDADD	=	-lrt

# LBS code
SRCS	=	main.c
SRCS	+=	dispatch.c
SRCS	+=	s3state.c
SRCS	+=	deleteto.c
SRCS	+=	findlast.c
SRCS	+=	objmap.c

# Data structures
.PATH.c	:	../lib/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
IDIRS	+=	-I ../lib/datastruct

# Utility functions
.PATH.c	:	../lib/util
SRCS	+=	asprintf.c
SRCS	+=	daemonize.c
SRCS	+=	hexify.c
SRCS	+=	monoclock.c
SRCS	+=	noeintr.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../lib/util

# Fundamental algorithms
.PATH.c	:	../lib/alg
SRCS	+=	crc32c.c
SRCS	+=	md5.c
IDIRS	+=	-I ../lib/alg

# Event loop
.PATH.c	:	../lib/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
IDIRS	+=	-I ../lib/events

# Event-driven networking
.PATH.c	:	../lib/network
SRCS	+=	network_buf.c
SRCS	+=	network_accept.c
IDIRS	+=	-I ../lib/network

# Buffered networking
.PATH.c	:	../lib/netbuf
SRCS	+=	netbuf_read.c
SRCS	+=	netbuf_write.c
IDIRS	+=	-I ../lib/netbuf

# Wire protocol
.PATH.c	:	../lib/wire
SRCS	+=	wire_packet.c
SRCS	+=	wire_readpacket.c
SRCS	+=	wire_requestqueue.c
SRCS	+=	wire_writepacket.c
IDIRS	+=	-I ../lib/wire

# S3 request/response packets
.PATH.c	:	../lib/proto_s3
SRCS	+=	proto_s3_client.c
IDIRS	+=	-I ../lib/proto_s3

# LBS request/response packets
.PATH.c	:	../lib/proto_lbs
SRCS	+=	proto_lbs_server.c
IDIRS	+=	-I ../lib/proto_lbs

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

# Program name
PROG	=	lbs

# Library code required
LDADD	=	-lpthread -lrt

# LBS code
SRCS	=	main.c
SRCS	+=	dispatch.c
SRCS	+=	dispatch_request.c
SRCS	+=	dispatch_response.c
SRCS	+=	worker.c
SRCS	+=	storage.c
SRCS	+=	storage_findfiles.c
SRCS	+=	storage_util.c
SRCS	+=	disk.c

# Data structures
.PATH.c	:	../lib/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
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
SRCS	+=	wire_writepacket.c
IDIRS	+=	-I ../lib/wire

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

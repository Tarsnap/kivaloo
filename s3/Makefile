# Program name
PROG	=	s3

# Library code required
LDADD	=	-lrt

# LBS code
SRCS	=	main.c
SRCS	+=	dns.c
SRCS	+=	dispatch.c

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
SRCS	+=	b64encode.c
SRCS	+=	daemonize.c
SRCS	+=	hexify.c
SRCS	+=	monoclock.c
SRCS	+=	noeintr.c
SRCS	+=	sock.c
SRCS	+=	sock_util.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../lib/util

# Logging code
.PATH.c	:	../lib/logging
SRCS	+=	logging.c
IDIRS	+=	-I ../lib/logging

# Fundamental algorithms
.PATH.c	:	../lib/alg
SRCS	+=	crc32c.c
SRCS	+=	md5.c
SRCS	+=	sha1.c
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
SRCS	+=	network_connect.c
IDIRS	+=	-I ../lib/network

# Buffered networking
.PATH.c	:	../lib/netbuf
SRCS	+=	netbuf_read.c
SRCS	+=	netbuf_write.c
IDIRS	+=	-I ../lib/netbuf

# HTTP client protocol
.PATH.c	:	../lib/http
SRCS	+=	http.c
IDIRS	+=	-I ../lib/http

# S3 client protocol and request queue
.PATH.c	:	../lib/s3
SRCS	+=	s3_request.c
SRCS	+=	s3_request_queue.c
SRCS	+=	s3_serverpool.c
SRCS	+=	s3_verifyetag.c
IDIRS	+=	-I ../lib/s3

# Wire protocol
.PATH.c	:	../lib/wire
SRCS	+=	wire_packet.c
SRCS	+=	wire_readpacket.c
SRCS	+=	wire_writepacket.c
IDIRS	+=	-I ../lib/wire

# S3 request/response packets
.PATH.c	:	../lib/proto_s3
SRCS	+=	proto_s3_server.c
IDIRS	+=	-I ../lib/proto_s3

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

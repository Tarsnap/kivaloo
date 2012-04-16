# Program name.
PROG	=	mux

# Library code required
LDADD	=	-lrt

# MUX code
SRCS	=	main.c
SRCS	+=	dispatch.c

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
SRCS	+=	wire_requestqueue.c
IDIRS	+=	-I ../lib/wire

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

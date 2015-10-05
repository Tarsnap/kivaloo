# Program name.
PROG	=	mux

# Libraries which are sometimes merged into libc
LDADD	=	-lrt
#LDADD	+=	-lxnet  # Missing on FreeBSD

# MUX code
SRCS	=	main.c
SRCS	+=	dispatch.c

# Data structures
.PATH.c	:	../libcperciva/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
IDIRS	+=	-I ../libcperciva/datastruct

# Utility functions
.PATH.c	:	../libcperciva/util
SRCS	+=	asprintf.c
SRCS	+=	daemonize.c
SRCS	+=	insecure_memzero.c
SRCS	+=	monoclock.c
SRCS	+=	noeintr.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../libcperciva/util

# Fundamental algorithms
.PATH.c	:	../libcperciva/alg
SRCS	+=	crc32c.c
IDIRS	+=	-I ../libcperciva/alg

# Event loop
.PATH.c	:	../libcperciva/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_network_selectstats.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
IDIRS	+=	-I ../libcperciva/events

# Event-driven networking
.PATH.c	:	../libcperciva/network
SRCS	+=	network_accept.c
SRCS	+=	network_read.c
SRCS	+=	network_write.c
IDIRS	+=	-I ../libcperciva/network

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

CFLAGS	+=	-Wno-unused-function

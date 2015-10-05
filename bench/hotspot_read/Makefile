PROG=	hotspot_read
SRCS=	main.c

# Libraries which are sometimes merged into libc
LDADD	=	-lrt
#LDADD	+=	-lxnet  # Missing on FreeBSD

# Data structures (libcperciva)
.PATH.c	:	../../libcperciva/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
IDIRS	+=	-I ../../libcperciva/datastruct

# Data structures
.PATH.c	:	../../lib/datastruct
SRCS	+=	kvldskey.c
IDIRS	+=	-I ../../lib/datastruct

# Utility functions
.PATH.c	:	../../libcperciva/util
SRCS	+=	insecure_memzero.c
SRCS	+=	monoclock.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../../libcperciva/util
.PATH.c	:	../lib/
SRCS	+=	mkpair.c
IDIRS	+=	-I ../lib

# Fundamental algorithms
.PATH.c	:	../../libcperciva/alg
SRCS	+=	crc32c.c
SRCS	+=	sha256.c
IDIRS	+=	-I ../../libcperciva/alg

# Event loop
.PATH.c	:	../../libcperciva/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_network_selectstats.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
IDIRS	+=	-I ../../libcperciva/events

# Event-driven networking
.PATH.c	:	../../libcperciva/network
SRCS	+=	network_read.c
SRCS	+=	network_write.c
IDIRS	+=	-I ../../libcperciva/network

# Buffered networking
.PATH.c	:	../../lib/netbuf
SRCS	+=	netbuf_read.c
SRCS	+=	netbuf_write.c
IDIRS	+=	-I ../../lib/netbuf

# Wire protocol
.PATH.c	:	../../lib/wire
SRCS	+=	wire_packet.c
SRCS	+=	wire_readpacket.c
SRCS	+=	wire_writepacket.c
SRCS	+=	wire_requestqueue.c
IDIRS	+=	-I ../../lib/wire

# KVLDS request/response packets
.PATH.c	:	../../lib/proto_kvlds
SRCS	+=	proto_kvlds_client.c
IDIRS	+=	-I ../../lib/proto_kvlds

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

CFLAGS	+=	-Wno-unused-function

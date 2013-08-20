# Program name.
PROG	=	kvlds

# Libraries which are sometimes merged into libc
LDADD	=	-lrt
#LDADD	+=	-lxnet  # Missing on FreeBSD

# KVLDS code
SRCS	=	main.c
SRCS	+=	dispatch.c
SRCS	+=	dispatch_mr.c
SRCS	+=	dispatch_nmr.c
SRCS	+=	btree.c
SRCS	+=	btree_balance.c
SRCS	+=	btree_cleaning.c
SRCS	+=	btree_mlen.c
SRCS	+=	btree_sync.c
SRCS	+=	btree_find.c
SRCS	+=	btree_mutate.c
SRCS	+=	btree_node.c
SRCS	+=	btree_node_split.c
SRCS	+=	btree_node_merge.c
SRCS	+=	serialize.c
SRCS	+=	node.c

# Debugging code
#SRCS	+=	btree_sanity.c
#CFLAGS	+=	-DSANITY_CHECKS
CLEANFILES+=	btree_sanity.o

# Data structures (libcperciva)
.PATH.c	:	../libcperciva/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
SRCS	+=	elasticqueue.c
SRCS	+=	seqptrmap.c
IDIRS	+=	-I ../libcperciva/datastruct

# Data structures
.PATH.c	:	../lib/datastruct
SRCS	+=	kvldskey.c
SRCS	+=	kvhash.c
SRCS	+=	kvpair.c
SRCS	+=	pool.c
IDIRS	+=	-I ../lib/datastruct

# Utility functions
.PATH.c	:	../libcperciva/util
SRCS	+=	asprintf.c
SRCS	+=	daemonize.c
SRCS	+=	humansize.c
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
SRCS	+=	network_buf.c
SRCS	+=	network_accept.c
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

# LBS request/response packets
.PATH.c	:	../lib/proto_lbs
SRCS	+=	proto_lbs_client.c
IDIRS	+=	-I ../lib/proto_lbs

# KVLDS request/response packet
.PATH.c	:	../lib/proto_kvlds
SRCS	+=	proto_kvlds_server.c
IDIRS	+=	-I ../lib/proto_kvlds

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

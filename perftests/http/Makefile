PROG=	test_http
SRCS=	main.c

# Data structures
.PATH.c	:	../../libcperciva/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
IDIRS	+=	-I ../../libcperciva/datastruct

# Utility functions
.PATH.c	:	../../libcperciva/util
SRCS	+=	monoclock.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../../libcperciva/util

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
SRCS	+=	network_connect.c
SRCS	+=	network_read.c
SRCS	+=	network_write.c
IDIRS	+=	-I ../../libcperciva/network

# Buffered networking
.PATH.c	:	../../lib/netbuf
SRCS	+=	netbuf_read.c
SRCS	+=	netbuf_write.c
IDIRS	+=	-I ../../lib/netbuf

# HTTP protocol
.PATH	:	../../lib/http
SRCS	+=	http.c
IDIRS	+=	-I ../../lib/http

CFLAGS	+=	-g

test:	test_http
	@./test_http www.google.com /
	@./test_http s3.amazonaws.com /
	@./test_http www.tarsnap.com /kivaloo.html

.include <bsd.prog.mk>

CFLAGS	+=	-Wno-unused-function

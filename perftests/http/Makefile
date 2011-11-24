PROG=	test_http
SRCS=	main.c

# Data structures
.PATH.c	:	../../lib/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
IDIRS	+=	-I ../../lib/datastruct

# Utility functions
.PATH.c	:	../../lib/util
SRCS	+=	monoclock.c
SRCS	+=	sock.c
SRCS	+=	warnp.c
IDIRS	+=	-I ../../lib/util

# Event loop
.PATH.c	:	../../lib/events
SRCS	+=	events_immediate.c
SRCS	+=	events_network.c
SRCS	+=	events_timer.c
SRCS	+=	events.c
IDIRS	+=	-I ../../lib/events

# Event-driven networking
.PATH.c	:	../../lib/network
SRCS	+=	network_buf.c
SRCS	+=	network_connect.c
IDIRS	+=	-I ../../lib/network

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

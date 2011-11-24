PROG=	test_s3
SRCS=	main.c

# Fundamental algorithms
.PATH.c	:	../../lib/alg
SRCS	+=	md5.c
SRCS	+=	sha1.c
IDIRS	+=	-I ../../lib/alg

# Data structures
.PATH.c	:	../../lib/datastruct
SRCS	+=	elasticarray.c
SRCS	+=	ptrheap.c
SRCS	+=	timerqueue.c
IDIRS	+=	-I ../../lib/datastruct

# Utility functions
.PATH.c	:	../../lib/util
SRCS	+=	asprintf.c
SRCS	+=	b64encode.c
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

# S3 protocol
.PATH	:	../../lib/s3
SRCS	+=	s3_request.c
IDIRS	+=	-I ../../lib/s3

CFLAGS	+=	-g

test:	test_s3
	./test_s3 `head -1 ~/.aws/aws_test_key` `tail -1 ~/.aws/aws_test_key`

.include <bsd.prog.mk>

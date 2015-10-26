PROG=	test_heap
SRCS=	main.c

.PATH	:	../../libcperciva/datastruct
SRCS	+=	ptrheap.c
SRCS	+=	elasticarray.c
IDIRS	+=	-I ../../libcperciva/datastruct
.PATH	:	../../libcperciva/util
SRCS	+=	warnp.c
IDIRS	+=	-I ../../libcperciva/util

# FreeBSD hides getline by default
CFLAGS	+=	-D_POSIX_C_SOURCE=200809L

test:	test_heap
	@echo -n "Heapsorting main.c..."
	@./test_heap < main.c > main.c.sorted
	@if LC_ALL=C sort < main.c | cmp - main.c.sorted; then	\
		echo " PASSED!";			\
		rm main.c.sorted;			\
	else						\
		echo " FAILED!";			\
	fi

.include <bsd.prog.mk>

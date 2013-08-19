PROG=	test_crc32
SRCS=	main.c

.PATH	:	../../libcperciva/alg
SRCS	+=	crc32c.c
IDIRS	+=	-I ../../libcperciva/alg

test:	test_crc32
	@./test_crc32

.include <bsd.prog.mk>


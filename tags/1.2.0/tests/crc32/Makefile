PROG=	test_crc32
SRCS=	main.c

.PATH	:	../../lib/alg
SRCS	+=	crc32c.c
IDIRS	+=	-I ../../lib/alg

test:	test_crc32
	@./test_crc32

.include <bsd.prog.mk>


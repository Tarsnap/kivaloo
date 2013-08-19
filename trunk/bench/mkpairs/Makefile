PROG=	mkpairs
SRCS=	main.c

# Utility functions
.PATH.c	:	../../libcperciva/util
SRCS	+=	warnp.c
IDIRS	+=	-I ../../libcperciva/util
.PATH.c	:	../lib/
SRCS	+=	mkpair.c
SRCS	+=	sha256.c
IDIRS	+=	-I ../lib

# Debugging options
#CFLAGS	+=	-g
#CFLAGS	+=	-DNDEBUG
#CFLAGS	+=	-DDEBUG
#CFLAGS	+=	-pg

.include <bsd.prog.mk>

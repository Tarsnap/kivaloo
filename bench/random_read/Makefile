.POSIX:
# AUTOGENERATED FILE, DO NOT EDIT
PROG=random_read
SRCS=main.c
IDIRS=-I ../../libcperciva/events -I ../../libcperciva/util -I ../../lib/bench -I ../../lib/datastruct -I ../../lib/proto_kvlds -I ../../lib/wire
SUBDIR_DEPTH=../..
RELATIVE_DIR=bench/random_read
LIBALL=../../liball/liball.a ../../liball/optional_mutex_normal/liball_optional_mutex_normal.a

all:
	if [ -z "$${HAVE_BUILD_FLAGS}" ]; then \
		cd ${SUBDIR_DEPTH}; \
		${MAKE} BUILD_SUBDIR=${RELATIVE_DIR} \
		    BUILD_TARGET=${PROG} buildsubdir; \
	else \
		${MAKE} ${PROG}; \
	fi

install:${PROG}
	mkdir -p ${BINDIR}
	cp ${PROG} ${BINDIR}/_inst.${PROG}.$$$$_ &&	\
	    strip ${BINDIR}/_inst.${PROG}.$$$$_ &&	\
	    chmod 0555 ${BINDIR}/_inst.${PROG}.$$$$_ && \
	    mv -f ${BINDIR}/_inst.${PROG}.$$$$_ ${BINDIR}/${PROG}
	if ! [ -z "${MAN1DIR}" ]; then			\
		mkdir -p ${MAN1DIR};			\
		for MPAGE in ${MAN1}; do						\
			cp $$MPAGE ${MAN1DIR}/_inst.$$MPAGE.$$$$_ &&			\
			    chmod 0444 ${MAN1DIR}/_inst.$$MPAGE.$$$$_ &&		\
			    mv -f ${MAN1DIR}/_inst.$$MPAGE.$$$$_ ${MAN1DIR}/$$MPAGE;	\
		done;									\
	fi

clean:
	rm -f ${PROG} ${SRCS:.c=.o}

${PROG}:${SRCS:.c=.o} ${LIBALL}
	${CC} -o ${PROG} ${SRCS:.c=.o} ${LIBALL} ${LDFLAGS} ${LDADD_EXTRA} ${LDADD_REQ} ${LDADD_POSIX}

main.o: main.c ../../lib/bench/bench.h ../../libcperciva/events/events.h ../../lib/datastruct/kvldskey.h ../../libcperciva/util/ctassert.h ../../lib/bench/mkpair.h ../../libcperciva/util/parsenum.h ../../lib/proto_kvlds/proto_kvlds.h ../../libcperciva/util/sock.h ../../libcperciva/util/warnp.h ../../lib/wire/wire.h
	${CC} ${CFLAGS_POSIX} -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -DCPUSUPPORT_CONFIG_FILE=\"cpusupport-config.h\" -DAPISUPPORT_CONFIG_FILE=\"apisupport-config.h\" -I../.. ${IDIRS} ${CPPFLAGS} ${CFLAGS} -c main.c -o main.o

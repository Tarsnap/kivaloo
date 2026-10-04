.POSIX:
PROG=test_storage_read_race
SUBDIR_DEPTH=../..
RELATIVE_DIR=tests/storage-read-race

all:
	if [ -z "$${HAVE_BUILD_FLAGS}" ]; then \
		cd ${SUBDIR_DEPTH}; \
		${MAKE} BUILD_SUBDIR=${RELATIVE_DIR} \
		    BUILD_TARGET=${PROG} buildsubdir; \
	else \
		${MAKE} ${PROG}; \
	fi

clean:
	rm -f ${PROG}

${PROG}: main.c
	${CC} ${CFLAGS_POSIX} -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	    -I../.. -I../../lbs -I../../libcperciva/datastruct \
	    -I../../libcperciva/util -I../../lib/proto_lbs \
	    ${CPPFLAGS} ${CFLAGS} main.c -o ${PROG} -lpthread ${LDADD_POSIX}

test: all
	@./${PROG}

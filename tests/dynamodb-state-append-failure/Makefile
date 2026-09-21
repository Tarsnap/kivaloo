.POSIX:
PROG=test_dynamodb_state_append_failure
SUBDIR_DEPTH=../..
RELATIVE_DIR=tests/dynamodb-state-append-failure

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
	    -I../.. -I../../lbs-dynamodb -I../../lib/proto_dynamodb_kv \
	    -I../../lib/proto_lbs -I../../libcperciva/events \
	    -I../../libcperciva/util -I../../lib/wire \
	    ${CPPFLAGS} ${CFLAGS} main.c -o ${PROG} ${LDADD_POSIX}

test: all
	@./${PROG}

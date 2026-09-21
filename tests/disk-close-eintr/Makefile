.POSIX:
PROG=test_disk_close_eintr
SUBDIR_DEPTH=../..
RELATIVE_DIR=tests/disk-close-eintr

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
	    -I../.. -I../../lbs -I../../libcperciva/util \
	    ${CPPFLAGS} ${CFLAGS} main.c -o ${PROG} ${LDADD_POSIX}

test: all
	@./${PROG}

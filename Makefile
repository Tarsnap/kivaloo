.POSIX:

PKG=	kivaloo
PROGS=	lbs kvlds mux s3 lbs-s3
TESTS=	tests/lbs tests/kvlds tests/mux tests/s3 tests/kvlds-s3 \
	perftests/kvldsperf perftests/kvldsclean perftests/http \
	perftests/s3 perftests/s3_put
BENCHES= bench/bulk_insert bench/bulk_update bench/bulk_extract	\
	bench/hotspot_read bench/random_mixed bench/random_read	\
	bench/mkpairs
BINDIR_DEFAULT=	/usr/local/bin
CFLAGS_DEFAULT=	-O2
TEST_CMD=	${MAKE} -C tests test

### Shared code between Tarsnap projects.

all:	cpusupport-config.h
	export CFLAGS="$${CFLAGS:-${CFLAGS_DEFAULT}}";	\
	export "LDADD_POSIX=`export CC=\"${CC}\"; cd ${LIBCPERCIVA_DIR}/POSIX && command -p sh posix-l.sh \"$$PATH\"`";	\
	export "CFLAGS_POSIX=`export CC=\"${CC}\"; cd ${LIBCPERCIVA_DIR}/POSIX && command -p sh posix-cflags.sh \"$$PATH\"`";	\
	. ./cpusupport-config.h;			\
	for D in ${PROGS} ${BENCHES} ${TESTS}; do	\
		( cd $${D} && ${MAKE} all ) || exit 2;	\
	done

cpusupport-config.h:
	if [ -e ${LIBCPERCIVA_DIR}/cpusupport/Build/cpusupport.sh ];	\
	then								\
		( export CC="${CC}"; command -p sh 			\
		    ${LIBCPERCIVA_DIR}/cpusupport/Build/cpusupport.sh	\
		    "$$PATH" ) > cpusupport-config.h;			\
	else								\
		: > cpusupport-config.h;				\
	fi

install: all
	export BINDIR=$${BINDIR:-${BINDIR_DEFAULT}};	\
	for D in ${PROGS}; do				\
		( cd $${D} && ${MAKE} install ) || exit 2;	\
	done

clean:
	for D in ${PROGS}; do					\
		( cd $${D} && ${MAKE} clean ) || exit 2;	\
	done

.PHONY:	test test-clean
test:	all
	${TEST_CMD}

test-clean:
	rm -rf tests-output/ tests-valgrind/

# Developer targets: These only work with BSD make
Makefiles:
	${MAKE} -f Makefile.BSD Makefiles

publish:
	${MAKE} -f Makefile.BSD publish

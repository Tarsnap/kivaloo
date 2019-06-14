.POSIX:

PKG=	kivaloo
PROGS=	lbs kvlds mux s3 lbs-s3 dynamodb-kv lbs-dynamodb	\
	kvlds-dump kvlds-undump
BENCHES= bench/bulk_insert bench/bulk_update bench/bulk_extract	\
	bench/hotspot_read bench/random_mixed bench/random_read	\
	bench/mkpairs
# For compatibility with other libcperciva software, we don't use
# ${BENCHES} in the shared code, so we add it to ${TESTS}.
TESTS=	tests/lbs tests/kvlds tests/mux tests/s3 tests/kvlds-s3 \
	tests/kvlds-blocking tests/kvlds-dump tests/kvlds-ddbkv \
	perftests/kvldsperf perftests/kvldsclean perftests/http \
	perftests/s3 perftests/s3_put perftests/serverpool	\
	perftests/dynamodb_sign perftests/dynamodb_request	\
	perftests/dynamodb_queue perftests/dynamodb_kv		\
	perftests/kvldsclean-ddbkv perftests/network-ssl	\
	${BENCHES}
BINDIR_DEFAULT=	/usr/local/bin
CFLAGS_DEFAULT=	-O2
LIBCPERCIVA_DIR=	libcperciva
TEST_CMD=	${MAKE} -C tests test

### Shared code between Tarsnap projects.

all:	cpusupport-config.h posix-flags.sh
	export CFLAGS="$${CFLAGS:-${CFLAGS_DEFAULT}}";	\
	. ./posix-flags.sh;				\
	. ./cpusupport-config.h;			\
	export HAVE_BUILD_FLAGS=1;			\
	for D in ${PROGS} ${TESTS}; do			\
		( cd $${D} && ${MAKE} all ) || exit 2;	\
	done

# For "loop-back" building of a subdirectory
buildsubdir: cpusupport-config.h posix-flags.sh
	. ./cpusupport-config.h; \
	. ./posix-flags.sh; \
	export HAVE_BUILD_FLAGS=1; \
	cd ${BUILD_SUBDIR} && ${MAKE} ${BUILD_TARGET}

posix-flags.sh:
	if [ -d ${LIBCPERCIVA_DIR}/POSIX/ ]; then			\
		export CC="${CC}";					\
		cd ${LIBCPERCIVA_DIR}/POSIX;				\
		printf "export \"LDADD_POSIX=";				\
		command -p sh posix-l.sh "$$PATH";			\
		printf "\"\n";						\
		printf "export \"CFLAGS_POSIX=";			\
		command -p sh posix-cflags.sh "$$PATH";			\
		printf "\"\n";						\
	else								\
		:;							\
	fi > $@

cpusupport-config.h:
	if [ -d ${LIBCPERCIVA_DIR}/cpusupport/ ]; then			\
		export CC="${CC}";					\
		command -p sh						\
		    ${LIBCPERCIVA_DIR}/cpusupport/Build/cpusupport.sh	\
		    "$$PATH";						\
	else								\
		:;							\
	fi > $@

install:	all
	export BINDIR=$${BINDIR:-${BINDIR_DEFAULT}};	\
	for D in ${PROGS}; do				\
		( cd $${D} && ${MAKE} install ) || exit 2;	\
	done

clean:
	rm -f cpusupport-config.h posix-flags.sh
	for D in ${PROGS} ${TESTS}; do				\
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

.POSIX:

PKG=	kivaloo
PROGS=	lbs kvlds mux s3 lbs-s3
TESTS=	tests perftests
BENCHES= bench/bulk_insert bench/bulk_update bench/bulk_extract	\
	bench/hotspot_read bench/random_mixed bench/random_read	\
	bench/mkpairs
BINDIR_DEFAULT=	/usr/local/bin
CFLAGS_DEFAULT=	-O2

all:
	export CFLAGS="$${CFLAGS:-${CFLAGS_DEFAULT}}";	\
	export LDADD_POSIX=`export CC=${CC}; cd libcperciva/POSIX && command -p sh posix-l.sh "$$PATH"`;	\
	export CFLAGS_POSIX=`export CC=${CC}; cd libcperciva/POSIX && command -p sh posix-cflags.sh "$$PATH"`;	\
	for D in ${PROGS}; do				\
		( cd $${D} && ${MAKE} all ) || exit 2;	\
	done

install: all
	export BINDIR=$${BINDIR:-${BINDIR_DEFAULT}};	\
	for D in ${PROGS}; do				\
		( cd $${D} && ${MAKE} install ) || exit 2;	\
	done

clean:
	for D in ${PROGS}; do					\
		( cd $${D} && ${MAKE} clean ) || exit 2;	\
	done

test:	all
	${MAKE} -C tests test

# Developer targets: These only work with BSD make
Makefiles:
	${MAKE} -f Makefile.BSD Makefiles

publish:
	${MAKE} -f Makefile.BSD publish

PROGS=	lbs kvlds mux
TESTS=	tests perftests
BENCHES= bench/bulk_insert bench/bulk_update bench/bulk_extract	\
	bench/hotspot_read bench/random_mixed bench/random_read	\
	bench/mkpairs
PUBLISH= ${PROGS} BUILDING CHANGELOG COPYRIGHT DESIGN INTERFACES STYLE lib bench

test:
	make -C tests test

.for D in ${PROGS} ${BENCHES}
kivaloo-${VERSION}/${D}/Makefile:
	echo '.POSIX:' > $@
	( cd ${D} && echo -n 'PROG=kivaloo-' && make -V PROG ) >> $@
	( cd ${D} && echo -n 'SRCS=' && make -V SRCS ) >> $@
	( cd ${D} && echo -n 'IDIRS=' && make -V IDIRS ) >> $@
	( cd ${D} && echo -n 'LDADD=' && make -V LDADD ) >> $@
	cat Makefile.prog >> $@
	( cd ${D} && make -V SRCS |	\
	    tr ' ' '\n' |		\
	    sed -E 's/.c$$/.o/' |	\
	    while read F; do		\
		echo -n "$${F}: ";	\
		make source-$${F};	\
	    done ) >> $@
.endfor

publish: clean
	if [ -z "${VERSION}" ]; then			\
		echo "VERSION must be specified!";	\
		exit 1;					\
	fi
	if find . | grep \~; then					\
		echo "Delete temporary files before publishing!";	\
		exit 1;							\
	fi
	rm -f kivaloo-${VERSION}.tgz
	mkdir kivaloo-${VERSION}
	tar -cf- --exclude 'Makefile.*' --exclude Makefile --exclude .svn ${PUBLISH} | \
	    tar -xf- -C kivaloo-${VERSION}
	cp Makefile.POSIX kivaloo-${VERSION}/Makefile
.for D in ${PROGS} ${BENCHES}
	make kivaloo-${VERSION}/${D}/Makefile
.endfor
	tar -cvzf kivaloo-${VERSION}.tgz kivaloo-${VERSION}
	rm -r kivaloo-${VERSION}

SUBDIR=	${PROGS} ${TESTS} ${BENCHES}
.include <bsd.subdir.mk>

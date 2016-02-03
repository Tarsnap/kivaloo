PKG=	kivaloo
PROGS=	lbs kvlds mux s3 lbs-s3
TESTS=	tests perftests
SUBST_VERSION_FILES=	lbs/main.c kvlds/main.c mux/main.c s3/main.c \
			lbs-s3/main.c
BENCHES= bench/bulk_insert bench/bulk_update bench/bulk_extract	\
	bench/hotspot_read bench/random_mixed bench/random_read	\
	bench/mkpairs
PUBLISH= ${PROGS} BUILDING CHANGELOG COPYRIGHT DESIGN INTERFACES STYLE POSIX lib libcperciva bench

test:	all
	${MAKE} -C tests test

.for D in ${PROGS} ${BENCHES}
${PKG}-${VERSION}/${D}/Makefile:
	echo '.POSIX:' > $@
	( cd ${D} && echo -n 'PROG=kivaloo-' && ${MAKE} -V PROG ) >> $@
	( cd ${D} && echo -n 'SRCS=' && ${MAKE} -V SRCS ) >> $@
	( cd ${D} && echo -n 'IDIRS=' && ${MAKE} -V IDIRS ) >> $@
	( cd ${D} && echo -n 'LDADD_REQ=' && ${MAKE} -V LDADD_REQ ) >> $@
	cat Makefile.prog >> $@
	( cd ${D} && ${MAKE} -V SRCS |	\
	    tr ' ' '\n' |		\
	    sed -E 's/.c$$/.o/' |	\
	    while read F; do		\
		S=`${MAKE} source-$${F}`;	\
		echo "$${F}: $${S}";	\
		echo "	\$${CC} \$${CFLAGS} \$${CFLAGS_POSIX} -D_POSIX_C_SOURCE=200809L \$${IDIRS} -c $${S} -o $${F}"; \
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
	rm -f ${PKG}-${VERSION}.tgz
	mkdir ${PKG}-${VERSION}
	tar -cf- --exclude 'Makefile.*' --exclude Makefile --exclude .svn ${PUBLISH} | \
	    tar -xf- -C ${PKG}-${VERSION}
	cp Makefile.POSIX ${PKG}-${VERSION}/Makefile
.for D in ${PROGS} ${BENCHES}
	${MAKE} ${PKG}-${VERSION}/${D}/Makefile
.endfor
.for F in ${SUBST_VERSION_FILES}
	# We need to write a temporary file because FreeBSD and GNU behaviour
	# of sed -i is different.
	sed -e 's/@VERSION@/${VERSION}/' -e 's/@DATE@/${RELEASEDATE}/' \
	    < ${PKG}-${VERSION}/${F} > ${PKG}-${VERSION}/${F}.tmp
	mv ${PKG}-${VERSION}/${F}.tmp ${PKG}-${VERSION}/${F}
.endfor
	tar -cvzf ${PKG}-${VERSION}.tgz ${PKG}-${VERSION}
	rm -r ${PKG}-${VERSION}

SUBDIR=	${PROGS} ${TESTS} ${BENCHES}
.include <bsd.subdir.mk>

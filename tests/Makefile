SUBDIR=	lbs kvlds mux s3 kvlds-s3

test:
.for D in ${SUBDIR}
	${MAKE} -C ${D} -f Makefile.BSD test
.endfor

.include <bsd.subdir.mk>

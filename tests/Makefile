.POSIX:

SUBDIR=	lbs kvlds mux s3 kvlds-s3 kvlds-ddbkv

test:
	for D in ${SUBDIR}; do				\
		${MAKE} -C $${D} test;			\
	done

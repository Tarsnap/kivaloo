.POSIX:

SUBDIR=	lbs kvlds mux s3 kvlds-s3 kvlds-ddbkv onlinequantile \
	btree-cleaning-failure

test:
	for D in ${SUBDIR}; do				\
		cd $${D} && ${MAKE} test && cd ..;	\
	done

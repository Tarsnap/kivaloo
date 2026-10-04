.POSIX:

SUBDIR=	lbs kvlds mux s3 kvlds-s3 kvlds-ddbkv onlinequantile storage-read-race

test:
	for D in ${SUBDIR}; do				\
		cd $${D} && ${MAKE} test && cd ..;	\
	done

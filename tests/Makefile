.POSIX:

SUBDIR=	lbs kvlds mux s3 kvlds-s3 kvlds-ddbkv onlinequantile

test:
	sh ./btree-cleaning-poke.sh
	for D in ${SUBDIR}; do				\
		cd $${D} && ${MAKE} test && cd ..;	\
	done

#!/bin/sh -e

scriptdir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)

start_kvlds () {
	mkdir $WRKDIR
	${scriptdir}/../lbs/lbs -d $WRKDIR -s $WRKDIR/sock_lbs -b 2048 -p $WRKDIR/lbs.pid
	${scriptdir}/../kvlds/kvlds -s $WRKDIR/sock_kvlds -l $WRKDIR/sock_lbs -p $WRKDIR/kvlds.pid
}
stop_kvlds () {
	kill $(cat $WRKDIR/kvlds.pid)
	kill $(cat $WRKDIR/lbs.pid)
	rm -rf $WRKDIR
}

if [ $# -ne 1 ]; then
	echo "usage: bench-local.sh DIRECTORY"
	exit 1
fi
mkdir -p $1
WRKDIR=$(realpath $1)/bench

# Testing bulk insert performance
start_kvlds
${scriptdir}/mkpairs/mkpairs $((1024 * 1024 * 130)) > $WRKDIR/pairs
${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds < $WRKDIR/pairs |
    awk '{print "bulk_insert: " $0}'
stop_kvlds

# Testing bulk update performance
seq 12 27 | perl -ne 'printf "%d\n", 2**$_' | while read X; do
	start_kvlds
	${scriptdir}/mkpairs/mkpairs $X > $WRKDIR/pairs.$X
	${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds < $WRKDIR/pairs.$X >/dev/null
	printf "bulk_update ${X} "
	${scriptdir}/bulk_update/bulk_update $WRKDIR/sock_kvlds < $WRKDIR/pairs.$X
	stop_kvlds
done

# Testing bulk extract performance
seq 12 27 | perl -ne 'printf "%d\n", 2**$_' | while read X; do
	start_kvlds
	${scriptdir}/mkpairs/mkpairs $X |		\
	    ${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds >/dev/null
	printf "bulk_extract ${X} "
	${scriptdir}/bulk_extract/bulk_extract $WRKDIR/sock_kvlds
	stop_kvlds
done

# Testing uniform random read performance
seq 12 27 | perl -ne 'printf "%d\n", 2**$_' | while read X; do
	start_kvlds
	${scriptdir}/mkpairs/mkpairs $X |
	    ${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds >/dev/null
	printf "random_read ${X} "
	${scriptdir}/random_read/random_read $WRKDIR/sock_kvlds $X
	stop_kvlds
done

# Testing uniform random mixed performance
seq 12 27 | perl -ne 'printf "%d\n", 2**$_' | while read X; do
	start_kvlds
	${scriptdir}/mkpairs/mkpairs $X |		\
	    ${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds >/dev/null
	printf "random_mixed ${X} "
	${scriptdir}/random_mixed/random_mixed $WRKDIR/sock_kvlds $X
	stop_kvlds
done

# Testing hot-spot read performance
seq 16 27 | perl -ne 'printf "%d\n", 2**$_' | while read X; do
	start_kvlds
	${scriptdir}/mkpairs/mkpairs $X |		\
	    ${scriptdir}/bulk_insert/bulk_insert $WRKDIR/sock_kvlds >/dev/null
	printf "hotspot_read ${X} "
	${scriptdir}/hotspot_read/hotspot_read $WRKDIR/sock_kvlds $X
	stop_kvlds
done

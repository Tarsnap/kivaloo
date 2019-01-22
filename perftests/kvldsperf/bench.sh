#!/bin/sh

jot 10 |
    while read X; do
	NODISK=YES ./test_kvldsperf.sh 2>&1;
done |				\
    awk '{print $2 " "}' |	\
    lam - - - - - - |		\
    rs -T |			\
    perl -pe 'chomp;
	@_ = sort {$a <=> $b} split;
	$_ = join " ", @_, "\n";' | 	\
    rs -T

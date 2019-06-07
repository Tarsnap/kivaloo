#!/bin/sh

for X in 1 2 3 4 5 6 7 8 9 10; do
	NODISK=YES ./test_kvldsperf.sh 2>&1;
done |				\
    awk '{print $2 " "}' |	\
    lam - - - - - - |		\
    rs -T |			\
    perl -pe 'chomp;
	@_ = sort {$a <=> $b} split;
	$_ = join " ", @_, "\n";' |	\
    rs -T

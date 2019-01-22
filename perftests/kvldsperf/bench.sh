#!/bin/sh

jot 10 |
    while read X; do
	NODISK=YES ./test_kvldsperf.sh 2>&1;
done |				\
    grep real |			\
    awk '{print $1}' |		\
    lam - - - - - - |		\
    tr 's' ' ' |		\
    rs -T |			\
    perl -pe 'chomp;
	@_ = sort {$a <=> $b} split;
	$_ = join " ", @_, "\n";' | 	\
    rs -T

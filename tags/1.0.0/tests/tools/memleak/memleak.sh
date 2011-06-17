#!/bin/sh

grep 'USER' $1 |
    tr -s ' ' |
    sed -E 's/^ +//' |
    cut -f 3,5- -d ' ' |
    perl -e '
	while (<>) {
		chomp;
		($t, $r) = split (/ /, $_, 2);
		if ($r =~ /(0x[0-9a-f]+) = malloc\(([0-9]+)\)/) {
			$AT{$1} = $t;
			$AS{$1} = $2;
		} elsif ($r =~ /free\((0x[0-9a-f]+)\)/) {
			delete $AT{$1};
			delete $AS{$1};
		} elsif ($r =~
		    /(0x[0-9a-f]+) = realloc\((0x[0-9a-f]+), ([0-9]+)\)/) {
			delete $AT{$2};
			delete $AS{$2};
			$AT{$1} = $t;
			$AS{$1} = $3;
		}
	}
	$N = 0;
	$T = 0;
	for $p (keys %AT) {
		print "$AT{$p}\n";
		$N += 1;
		$T += $AS{$p};
	}
	print STDERR "\n$N allocations leaked $T bytes\n\n";
    ' | perl -e '
	open F, "<", $ARGV[0];
	while (<F>) {
		chomp;
		$L{$_} = 1;
	};
	close F;
	open F, "<", $ARGV[1];
	while (<F>) {
		next if (/CALL  utrace/);
		next if (/RET   utrace 0/);
		next if ((/([0-9.]+) USER/) && ($L{$1} eq ""));
		s/ USER / LEAK /;
		print;
	}' /dev/stdin $1 > $2

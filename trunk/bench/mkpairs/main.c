#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mkpair.h"
#include "warnp.h"

static int
compar(const void * _x, const void * _y)
{

	return (memcmp(_x, _y, 80));
}

int
main(int argc, char * argv[])
{
	uintmax_t N;
	uint64_t X, Y;
	uint8_t * buf;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: mkpairs N\n");
		exit(1);
	}

	/* Parse N. */
	if ((N = strtoumax(argv[1], NULL, 0)) == 0) {
		warnp("Invalid value for N: %s", argv[1]);
		exit(1);
	}

	/* Allocate buffer for 2^16 key-value pairs. */
	if ((buf = malloc(40 * 2 * 65536)) == NULL) {
		warnp("malloc");
		exit(1);
	}

	/* Create groups of key-value pairs. */
	for (X = 0; (X << 16) < N; X++) {
		/* Generate key-value pairs. */
		for (Y = 0; ((X << 16) + Y < N) && (Y < (1 << 16)); Y++) {
			mkkey(X, Y, &buf[Y * 80]);
			mkval(X, Y, &buf[Y * 80 + 40]);
		}

		/* Sort key-value pairs. */
		qsort(buf, Y, 80, compar);

		/* Write out key-value pairs. */
		if (fwrite(buf, 80, Y, stdout) < Y) {
			warnp("fwrite");
			exit(1);
		}
	}

	/* Free buffer. */
	free(buf);

	/* Success! */
	exit(0);
}

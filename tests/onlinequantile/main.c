#include <stdio.h>
#include <string.h>

#include "onlinequantile.h"
#include "parsenum.h"
#include "warnp.h"

#define LINELEN 80

/* Return 0 if ok, 1 on EOF, -1 on error. */
static int
parse_line(FILE * fp, char * instr, double * val)
{
	char line[LINELEN];

	/* Repeat until we have a non-comment, non-blank line. */
	do {
		if (fgets(line, LINELEN, fp) == NULL) {
			/* Handle error or EOF. */
			if (ferror(fp)) {
				warnp("fgets");
				goto err0;
			}
			goto eof;
		}

		/* If we have a newline, remove it. */
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = '\0';
	} while ((line[0] == '#') || (line[0] == '\0'));

	/* Sanity-check the format.  Multiple tabs characters are ok. */
	if (line[1] != '\t') {
		warn0("Unrecognized character in line: %s", line);
		goto err0;
	}

	/* Get instruction and value from the line. */
	*instr = line[0];
	if (PARSENUM(val, &line[2])) {
		warnp("parsenum");
		goto err0;
	}

	/* Success! */
	return (0);

eof:
	/* End of file! */
	return (1);

err0:
	/* Failure! */
	return (-1);
}

static int
handle_line(struct onlinequantile * oq, char instr, double val)
{
	double x;

	switch (instr) {
	case 'a':
		/* Add value from file. */
		if (onlinequantile_add(oq, val)) {
			warn0("onlinequantile_add");
			goto err0;
		}
		break;
	case 'g':
		/* Get the online quantile, and check against expected value. */
		if (onlinequantile_get(oq, &x)) {
			warn0("onlinequantile_get");
			goto err0;
		}
		if (fabs(x - val) >= 0.001) {
			warn0("unexpected value: %g instead of %g", x, val);
			goto err0;
		}
		break;
	default:
		warn0("Unrecognized instruction: %c", instr);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
process_test(const char * filename)
{
	struct onlinequantile * oq;
	FILE * fp;
	char instr;
	double val;

	/* Open file. */
	if ((fp = fopen(filename, "r")) == NULL) {
		warnp("fopen");
		goto err0;
	}

	/* Get first line and initialize with the desired quantile. */
	if (parse_line(fp, &instr, &val)) {
		warnp("parse_line");
		goto err1;
	}
	if (instr != 'q') {
		warn0("Invalid file; first instruction must be 'q'");
		goto err1;
	}
	if ((oq = onlinequantile_init(val)) == NULL) {
		warn0("onlinequantile_init");
		goto err1;
	}

	/* The first time should return 1. */
	if (onlinequantile_get(oq, &val) != 1) {
		warn0("onlinequantile_get improper handling of empty list");
		goto err2;
	}

	/* Process lines in file. */
	while (parse_line(fp, &instr, &val) == 0) {
		/* Process data. */
		if (handle_line(oq, instr, val))
			goto err2;
	}

	/* Clean up. */
	onlinequantile_free(oq);
	if (fclose(fp)) {
		warnp("fclose");
		goto err0;
	}

	/* Success! */
	return (0);

err2:
	onlinequantile_free(oq);
err1:
	if (fclose(fp))
		warnp("fclose");
err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{

	WARNP_INIT;
	(void)argc; /* UNUSED */

	/* Check number of arguments. */
	if (argc < 2) {
		fprintf(stderr, "usage: test_onlinequantile <filename>\n");
		goto err0;
	}

	/* Run test. */
	if (process_test(argv[1]))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (1);
}

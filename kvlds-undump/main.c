#include <sys/stat.h>

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "events.h"
#include "getopt.h"
#include "kivaloo.h"
#include "kvlds.h"
#include "kvldskey.h"
#include "monoclock.h"
#include "warnp.h"

struct undumpstate {
	DIR * d;
	uint64_t N;
};

static struct kvldskey *
readfile(const char * dir, const char * fname)
{
	char * s;
	FILE * f;
	struct stat sb;
	uint8_t buf[255];
	struct kvldskey * k;

	/* Construct file name. */
	if (asprintf(&s, "%s/%s", dir, fname) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Open file. */
	if ((f = fopen(s, "rb")) == NULL) {
		warnp("fopen(%s)", s);
		goto err1;
	}

	/* Figure out how large the file is. */
	if (fstat(fileno(f), &sb)) {
		warnp("fstat(%s)", s);
		goto err2;
	}

	/* Sanity-check. */
	if (!S_ISREG(sb.st_mode)) {
		warn0("Not a regular file: %s", s);
		goto err2;
	}
	if (sb.st_size > 255) {
		warn0("File is too large (%ju bytes): %s",
		    (intmax_t)(sb.st_size), s);
		goto err2;
	}

	/* Read value. */
	if (fread(buf, (size_t)sb.st_size, 1, f) != 1) {
		warnp("fread(%s)", s);
		goto err2;
	}

	/* Construct kvlds key. */
	if ((k = kvldskey_create(buf, (size_t)sb.st_size)) == NULL)
		goto err2;

	/* Close file and free file name. */
	fclose(f);
	free(s);

	/* Success! */
	return (k);

err2:
	fclose(f);
err1:
	free(s);
err0:
	/* Failure! */
	return (NULL);
}

static int
callback_pair(void * cookie, struct kvldskey ** key, struct kvldskey ** value)
{
	struct undumpstate * C = cookie;
	struct dirent * d;
	uint8_t len;
	uint8_t buf[255];

	/* Filesystem or stdout? */
	if (C->d != NULL) {
		/* Find a directory. */
		do {
			errno = 0;
			if ((d = readdir(C->d)) == NULL) {
				if (errno == 0)
					goto nomore;
				warnp("readdir");
				goto err0;
			}

			/* Skip "." and "..". */
			if ((strcmp(d->d_name, ".") != 0) &&
			    (strcmp(d->d_name, "..") != 0))
				break;
		} while(1);

		/* Read key and value. */
		if ((*key = readfile(d->d_name, "k")) == NULL)
			goto err0;
		if ((*value = readfile(d->d_name, "v")) == NULL)
			goto err1;
	} else {
		/* Read key from stdin. */
		if (fread(&len, 1, 1, stdin) != 1) {
			if (feof(stdin))
				goto nomore;
			goto err0;
		}
		if (fread(&buf, len, 1, stdin) != 1)
			goto err0;
		if ((*key = kvldskey_create(buf, len)) == NULL)
			goto err0;

		/* Read value from stdin. */
		if (fread(&len, 1, 1, stdin) != 1)
			goto err1;
		if (fread(&buf, len, 1, stdin) != 1)
			goto err1;
		if ((*value = kvldskey_create(buf, len)) == NULL)
			goto err1;
	}

	/* Done another key-value pair. */
	C->N++;

	/* Success! */
	return (0);

nomore:
	/* No more key-value pairs; but we were successful. */
	*key = *value = NULL;
	return (0);

err1:
	kvldskey_free(*key);
err0:
	/* Failure! */
	return (-1);
}

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-kvlds-undump -t <kvlds socket>"
	    " [--fs <dir>]\n");
	fprintf(stderr, "       kivaloo-kvlds-undump --version\n");
	exit(1);
}

/* Macro to simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: %s %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{
	/* State variables. */
	void * K;
	struct wire_requestqueue * Q;
	struct undumpstate C;
	struct timeval st, en;

	/* Command-line parameters. */
	char * opt_fs = NULL;
	char * opt_t = NULL;
	int opt_v = 0;

	/* Working variables. */
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("--fs"):
			if (opt_fs != NULL)
				usage();
			if ((opt_fs = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-t"):
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-v"):
			opt_v++;
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-kvlds-undump @VERSION@\n");
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s\n", ch);
			usage();
		GETOPT_DEFAULT:
			warn0("illegal option -- %s\n", ch);
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 0)
		usage();
	(void)argv; /* argv is not used beyond this point. */

	/* Sanity-check options. */
	if (opt_t == NULL)
		usage();

	/* Open a connection to KVLDS. */
	if ((K = kivaloo_open(opt_t, &Q)) == NULL) {
		warnp("Could not connect to KVLDS daemon");
		exit(1);
	}

	/*
	 * If we're reading from the filesystem, move to that directory and
	 * start reading it.
	 */
	if (opt_fs) {
		if (chdir(opt_fs)) {
			warnp("chdir(%s)", opt_fs);
			exit(1);
		}
		if ((C.d = opendir(".")) == NULL) {
			warnp("opendir(.)");
			exit(1);
		}
	}

	/* Prepare for undumping key-value pairs. */
	C.N = 0;
	if (!opt_fs)
		C.d = NULL;

	/* Get timestamp. */
	if (monoclock_get(&st)) {
		warnp("monoclock_get");
		exit(1);
	}

	/* Store many key-value pairs. */
	if (kvlds_multiset(Q, callback_pair, &C)) {
		warnp("Error occurred while writing key-value pairs");
		exit(1);
	}

	/* Get timestamp. */
	if (monoclock_get(&en)) {
		warnp("monoclock_get");
		exit(1);
	}

	/* Print statistics if appropriate. */
	if (opt_v) {
		fprintf(stderr,
		    "Stored %" PRIu64 " key-value pairs in %f seconds.",
		    C.N, timeval_diff(en, st));
	}

	/* Close the directory (if applicable). */
	if (opt_fs) {
		if (closedir(C.d)) {
			warnp("closedir");
			exit(1);
		}
	}

	/* Close the connection to KVLDS and the events layer. */
	kivaloo_close(K);
	events_shutdown();

	/* Free option strings. */
	free(opt_t);
	free(opt_fs);

	/* Success! */
	exit(0);
}

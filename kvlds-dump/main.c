#include <sys/stat.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "getopt.h"
#include "kivaloo.h"
#include "kvlds.h"
#include "kvldskey.h"
#include "monoclock.h"
#include "warnp.h"

struct dumpstate {
	int tofs;
	uint64_t N;
};

static int
writefile(const char * dir, const char * fname, const struct kvldskey * v)
{
	char * s;
	FILE * f;

	/* Construct file name. */
	if (asprintf(&s, "%s/%s", dir, fname) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Open file. */
	if ((f = fopen(s, "wb")) == NULL) {
		warnp("fopen(%s)", s);
		goto err1;
	}

	/* Write value. */
	if (fwrite(v->buf, v->len, 1, f) != 1) {
		warnp("fwrite(%s)", s);
		goto err2;
	}

	/* Close file and free file name. */
	if (fclose(f))
		warnp("fclose");
	free(s);

	/* Success! */
	return (0);

err2:
	if (fclose(f))
		warnp("fclose");
err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_pair(void * cookie,
    const struct kvldskey * key, const struct kvldskey * value)
{
	struct dumpstate * C = cookie;
	char kvnum[17];

	/* Filesystem or stdout? */
	if (C->tofs) {
		sprintf(kvnum, "%016" PRIx64, C->N);
		if (mkdir(kvnum, 0700)) {
			warnp("mkdir(%s)", kvnum);
			goto err0;
		}
		if (writefile(kvnum, "k", key))
			goto err0;
		if (writefile(kvnum, "v", value))
			goto err0;
	} else {
		if (fwrite(&key->len, 1, 1, stdout) != 1)
			goto err0;
		if (fwrite(key->buf, key->len, 1, stdout) != 1)
			goto err0;
		if (fwrite(&value->len, 1, 1, stdout) != 1)
			goto err0;
		if (fwrite(value->buf, value->len, 1, stdout) != 1)
			goto err0;
	}

	/* Done another key-value pair. */
	C->N++;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-kvlds-dump -t <kvlds socket>"
	    " [--fs <dir>]\n");
	fprintf(stderr, "       kivaloo-kvlds-dump --version\n");
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
	struct dumpstate C;
	struct timeval st, en;

	/* Command-line parameters. */
	char * opt_fs = NULL;
	char * opt_t = NULL;
	int opt_v = 0;

	/* Working variables. */
	const char * ch;
	struct kvldskey * nullkey;

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
			fprintf(stderr, "kivaloo-kvlds-dump @VERSION@\n");
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s", ch);
			usage();
		GETOPT_DEFAULT:
			warn0("illegal option -- %s", ch);
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

	/* If we're writing to the filesystem, move to that directory. */
	if (opt_fs) {
		if (chdir(opt_fs)) {
			warnp("chdir(%s)", opt_fs);
			exit(1);
		}
	}

	/* Prepare for dumping key-value pairs. */
	if ((nullkey = kvldskey_create(NULL, 0)) == NULL) {
		warnp("kvldskey_create");
		exit(1);
	}
	C.tofs = opt_fs ? 1 : 0;
	C.N = 0;

	/* Get timestamp. */
	if (monoclock_get(&st)) {
		warnp("monoclock_get");
		exit(1);
	}

	/* Read the range. */
	if (kvlds_range(Q, nullkey, nullkey, callback_pair, &C)) {
		warnp("Error occurred while reading key-value pairs");
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
		    "Dumped %" PRIu64 " key-value pairs in %f seconds.",
		    C.N, timeval_diff(en, st));
	}

	/* Free memory allocated by kvldskey_create. */
	kvldskey_free(nullkey);

	/* Close the connection to KVLDS. */
	kivaloo_close(K);

	/* Free option strings. */
	free(opt_t);
	free(opt_fs);

	/* Success! */
	exit(0);
}

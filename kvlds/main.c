#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "events.h"
#include "getopt.h"
#include "humansize.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#include "btree.h"
#include "dispatch.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-kvlds "
	    "-s <kvlds socket> -l <lbs socket> "
	    "[-C <npages> | -c <pagemem>] [-1] "
	    "[-k <max key length>] [-v <max value length>] [-p <pidfile>] "
	    "[-S <cost of storage per GB-month>] "
	    "[-w <commit delay time>] [-g <min forced commit size>]\n");
	fprintf(stderr, "       kivaloo-kvlds --version\n");
	exit(1);
}

/* Two macros to simplify error-handling in command-line parse loop. */
#define OPT_EINVAL(opt, arg) do {					\
	warn0("Cannot parse option: %s %s", opt, arg);			\
	exit(1);							\
} while (0)
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: %s %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{
	/* State variables. */
	struct wire_requestqueue * Q_lbs;
	struct btree * T;
	struct dispatch_state * dstate;
	int s;
	int s_lbs;

	/* Command-line parameters. */
	uint64_t opt_C = (uint64_t)(-1);
	uint64_t opt_c = (uint64_t)(-1);
	uint64_t opt_g = (uint64_t)(-1);
	uint64_t opt_k = (uint64_t)(-1);
	char * opt_l = NULL;
	char * opt_p = NULL;
	double opt_S = 1.0;
	char * opt_s = NULL;
	uint64_t opt_v = (uint64_t)(-1);
	double opt_w = 0.0;
	int opt_1 = 0;

	/* Working variables. */
	struct sock_addr ** sas_s;
	struct sock_addr ** sas_l;
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-C"):
			if (opt_C != (uint64_t)(-1))
				usage();
			if (humansize_parse(optarg, &opt_C))
				OPT_EINVAL(ch, optarg);
			break;
		GETOPT_OPTARG("-c"):
			if (opt_c != (uint64_t)(-1))
				usage();
			if (humansize_parse(optarg, &opt_c))
				OPT_EINVAL(ch, optarg);
			break;
		GETOPT_OPTARG("-g"):
			if (opt_g != (uint64_t)(-1))
				usage();
			if (humansize_parse(optarg, &opt_g))
				OPT_EINVAL(ch, optarg);
			break;
		GETOPT_OPTARG("-k"):
			if (opt_k != (uint64_t)(-1))
				usage();
			if (humansize_parse(optarg, &opt_k))
				OPT_EINVAL(ch, optarg);
			break;
		GETOPT_OPTARG("-l"):
			if (opt_l != NULL)
				usage();
			if ((opt_l = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-p"):
			if (opt_p != NULL)
				usage();
			if ((opt_p = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-S"):
			if (opt_S != 1.0)
				usage();
			opt_S = strtod(optarg, NULL);
			break;
		GETOPT_OPTARG("-s"):
			if (opt_s != NULL)
				usage();
			if ((opt_s = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-v"):
			if (opt_v != (uint64_t)(-1))
				usage();
			if (humansize_parse(optarg, &opt_v))
				OPT_EINVAL(ch, optarg);
			break;
		GETOPT_OPTARG("-w"):
			if (opt_w != 0.0)
				usage();
			opt_w = strtod(optarg, NULL);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-kvlds @VERSION@\n");
			exit(0);
		GETOPT_OPT("-1"):
			if (opt_1 != 0)
				usage();
			opt_1 = 1;
			break;
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
	if (opt_s == NULL)
		usage();
	if (opt_l == NULL)
		usage();
	if ((opt_C != (uint64_t)(-1)) && (opt_c != (uint64_t)(-1)))
		usage();
	if ((opt_C != (uint64_t)(-1)) &&
	    ((opt_C < 1024) || (opt_C > 1024 * 1024 * 1024))) {
		warn0("Cache size in pages must be in [2^10, 2^30]");
		exit(1);
	}
	if ((opt_k != (uint64_t)(-1)) && (opt_k > 255)) {
		warn0("Keys longer than 255 bytes are not supported");
		exit(1);
	}
	if ((opt_v != (uint64_t)(-1)) && (opt_v > 255)) {
		warn0("Values longer than 255 bytes are not supported");
		exit(1);
	}
	if ((opt_w < 0.0) || (opt_w > 1.0)) {
		warn0("Commit delay time in [0.0, 1.0]: -w %f", opt_w);
		exit(1);
	}
	if ((opt_g != (uint64_t)(-1)) &&
	    ((opt_g < 1) || (opt_g > 1024))) {
		warn0("Forced commit size must be in [1, 1024]: "
		    "-g %" PRIu64, opt_g);
		exit(1);
	}

	/* Resolve listening address. */
	if ((sas_s = sock_resolve(opt_s)) == NULL) {
		warnp("Error resolving socket address: %s", opt_s);
		exit(1);
	}
	if (sas_s[0] == NULL) {
		warn0("No addresses found for %s", opt_s);
		exit(1);
	}

	/* Resolve LBS address. */
	if ((sas_l = sock_resolve(opt_l)) == NULL) {
		warnp("Error resolving socket address: %s", opt_l);
		exit(1);
	}
	if (sas_l[0] == NULL) {
		warn0("No addresses found for %s", opt_l);
		exit(1);
	}

	/* Create and bind a socket, and mark it as listening. */
	if (sas_s[1] != NULL)
		warn0("Listening on first of multiple addresses found for %s",
		    opt_s);
	if ((s = sock_listener(sas_s[0])) == -1)
		exit(1);

	/* Create a socket, connect to the LBS, and mark it non-blocking. */
	if ((s_lbs = sock_connect(sas_l)) == -1)
		exit(1);

	/* Create a queue of requests to the block store. */
	if ((Q_lbs = wire_requestqueue_init(s_lbs)) == NULL) {
		warnp("Cannot create LBS request queue");
		exit(1);
	}

	/* Initialize the B+Tree. */
	if ((T =
	    btree_init(Q_lbs, opt_C, opt_c, &opt_k, &opt_v, opt_S)) == NULL) {
		warnp("Cannot initialize B+Tree");
		exit(1);
	}

	/* Daemonize and write pid. */
	if (opt_p == NULL) {
		if (asprintf(&opt_p, "%s.pid", opt_s) == -1) {
			warnp("asprintf");
			exit(1);
		}
	}
	if (daemonize(opt_p)) {
		warnp("Failed to daemonize");
		exit(1);
	}

	/* Handle connections, one at a time. */
	do {
		/* Accept a connection. */
		if ((dstate = dispatch_accept(s, T,
		    (size_t)opt_k, (size_t)opt_v, opt_w, (size_t)opt_g))
		    == NULL)
			exit(1);

		/* Loop until the connection is dead. */
		do {
			if (events_run()) {
				warnp("Error running event loop");
				exit(1);
			}
		} while (dispatch_alive(dstate));

		/* Close and free the connection. */
		if (dispatch_done(dstate))
			exit(1);
	} while (opt_1 == 0);

	/* Free the B+Tree. */
	btree_free(T);

	/* Shut down the LBS request queue. */
	wire_requestqueue_destroy(Q_lbs);
	wire_requestqueue_free(Q_lbs);

	/* Close sockets. */
	close(s_lbs);
	close(s);

	/* Free socket addresses. */
	sock_addr_freelist(sas_l);
	sock_addr_freelist(sas_s);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Free option strings. */
	free(opt_l);
	free(opt_p);
	free(opt_s);

	/* Success! */
	return (0);
}

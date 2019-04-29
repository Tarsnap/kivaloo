#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "events.h"
#include "getopt.h"
#include "parsenum.h"
#include "sock.h"
#include "warnp.h"

#include "dispatch.h"
#include "storage.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-lbs -s <lbs socket> -d <storage dir> "
	    "-b <block size> [-n <# of readers>] [-p <pidfile>] "
	    "[-1] [-L] [-l <read latency in ns>]\n");
	fprintf(stderr, "       kivaloo-lbs --version\n");
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
	struct storage_state * S;
	struct dispatch_state * D;
	int s;

	/* Command-line parameters. */
	char * opt_s = NULL;
	char * opt_d = NULL;
	size_t opt_b = (size_t)(-1);
	size_t opt_n = 16;
	char * opt_p = NULL;
	int opt_1 = 0;
	long opt_l = 0;
	int opt_L = 0;

	/* Working variables. */
	struct sock_addr ** sas;
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-b"):
			if (opt_b != (size_t)(-1))
				usage();
			if (PARSENUM(&opt_b, optarg, 512, 128 * 1024)) {
				warn0("Block size must be in [2^9, 2^17]");
				exit(1);
			}
			break;
		GETOPT_OPTARG("-d"):
			if (opt_d != NULL)
				usage();
			if ((opt_d = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-l"):
			if (opt_l != 0)
				usage();
			if (PARSENUM(&opt_l, optarg, 0, 999999999)) {
				warn0("Read latency must be in [0, 10^9) ns");
				exit(1);
			}
			break;
		GETOPT_OPT("-L"):
			if (opt_L != 0)
				usage();
			opt_L = 1;
			break;
		GETOPT_OPTARG("-n"):
			if (opt_n != 16)
				usage();
			if (PARSENUM(&opt_n, optarg, 1, 1000)) {
				warn0("Number of readers must be in [1, 1000]");
				exit(1);
			}
			break;
		GETOPT_OPTARG("-p"):
			if (opt_p != NULL)
				usage();
			if ((opt_p = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-s"):
			if (opt_s != NULL)
				usage();
			if ((opt_s = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-lbs @VERSION@\n");
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
	if (opt_d == NULL)
		usage();
	if (opt_b == (size_t)(-1))
		usage();

	/* Resolve the listening address. */
	if ((sas = sock_resolve(opt_s)) == NULL) {
		warnp("Error resolving socket address: %s", opt_s);
		exit(1);
	}
	if (sas[0] == NULL) {
		warn0("No addresses found for %s", opt_s);
		exit(1);
	}

	/* Create and bind a socket, and mark it as listening. */
	if (sas[1] != NULL)
		warn0("Listening on first of multiple addresses found for %s",
		    opt_s);
	if ((s = sock_listener(sas[0])) == -1)
		exit(1);

	/* Initialize the storage back-end. */
	if ((S = storage_init(opt_d, opt_b, opt_l, opt_L)) == NULL) {
		warnp("Error initializing storage directory: %s", opt_d);
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

	/* Initialize the dispatcher. */
	if ((D = dispatch_init(S, opt_b, opt_n)) == NULL) {
		warnp("Error initializing work dispatcher");
		exit(1);
	}

	/* Handle connections, one at once. */
	do {
		/* Get a connection and perform associated initialization. */
		if (dispatch_accept(D, s)) {
			warnp("Error accepting new connection");
			exit(1);
		}

		/* Loop until the connection dies. */
		do {
			if (events_run()) {
				warnp("Error running event loop");
				exit(1);
			}
		} while (dispatch_alive(D));

		/* Clean up the connection. */
		if (dispatch_close(D))
			exit(1);
	} while (opt_1 == 0);

	if (dispatch_done(D)) {
		warnp("Failed to shut down dispatcher");
		exit(1);
	}

	/* We're done with the storage state. */
	storage_done(S);

	/* Close the listening socket. */
	close(s);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Free the address structures. */
	sock_addr_freelist(sas);

	/* Free option strings. */
	free(opt_s);
	free(opt_p);
	free(opt_d);

	/* Success! */
	return (0);
}

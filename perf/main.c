#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "events.h"
#include "getopt.h"
#include "logging.h"
#include "parsenum.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#include "dispatch.h"
#include "perfstats.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-perf %s %s %s %s %s %s\n",
	    "-t <target socket>", "-s <source socket>", "-l <logfile>",
	    "[-w secs]", "[-p <pidfile>]", "[-1]");
	fprintf(stderr, "       kivaloo-perf --version\n");
	exit(1);
}

/* Simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: %s %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{
	/* State variables. */
	int sock_s;
	int sock_t;
	struct wire_requestqueue * Q_t;
	struct logging_file * logfile;
	struct perfstats * P;
	struct dispatch_state * dstate;

	/* Command-line parameters. */
	char * opt_l = NULL;
	char * opt_p = NULL;
	char * opt_s = NULL;
	char * opt_t = NULL;
	long opt_w = -1;
	int opt_1 = 0;

	/* Working variables. */
	struct sock_addr ** sas_s;
	struct sock_addr ** sas_t;
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
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
		GETOPT_OPTARG("-s"):
			if (opt_s != NULL)
				usage();
			if ((opt_s = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-t"):
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-w"):
			if (opt_w != -1)
				usage();
			if (PARSENUM(&opt_w, optarg, 1, 86400)) {
				warn0("Invalid option: %s %s", ch, optarg);
				usage();
			}
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-perf @VERSION@\n");
			exit(0);
		GETOPT_OPT("-1"):
			if (opt_1 != 0)
				usage();
			opt_1 = 1;
			break;
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
	if (opt_l == NULL)
		usage();
	if (opt_s == NULL)
		usage();
	if (opt_t == NULL)
		usage();

	/* Set default value. */
	if (opt_w == -1)
		opt_w = 60;

	/* Resolve listening address. */
	if ((sas_s = sock_resolve(opt_s)) == NULL) {
		warnp("Error resolving socket address: %s", opt_s);
		exit(1);
	}
	if (sas_s[0] == NULL) {
		warn0("No addresses found for %s", opt_s);
		exit(1);
	}

	/* Resolve target address. */
	if ((sas_t = sock_resolve(opt_t)) == NULL) {
		warnp("Error resolving socket address: %s", opt_t);
		exit(1);
	}
	if (sas_t[0] == NULL) {
		warn0("No addresses found for %s", opt_t);
		exit(1);
	}

	/* Create and bind a socket, and mark it as listening. */
	if (sas_s[1] != NULL)
		warn0("Listening on first of multiple addresses found for %s",
		    opt_s);
	if ((sock_s = sock_listener(sas_s[0])) == -1)
		exit(1);

	/* Connect to the target. */
	if ((sock_t = sock_connect(sas_t)) == -1)
		exit(1);

	/* Create a queue of requests to the target. */
	if ((Q_t = wire_requestqueue_init(sock_t)) == NULL) {
		warnp("Cannot create request queue");
		exit(1);
	}

	/* Open log file. */
	if ((logfile = logging_open(opt_l)) == NULL) {
		warnp("Cannot open log file");
		exit(1);
	}

	/* Initialize performance tracking state. */
	if ((P = perfstats_init(logfile, opt_w)) == NULL) {
		warnp("Cannot initialize performance statistics");
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
		if ((dstate = dispatch_accept(sock_s, Q_t, P)) == NULL)
			exit(1);

		/* Loop until the connection is dead. */
		do {
			if (events_run()) {
				warnp("Error running event loop");
				exit(1);
			}
		} while (dispatch_alive(dstate));

		/* Close and free the connection. */
		dispatch_done(dstate);
        } while (opt_1 == 0);

	/* Output and free performance tracking state */
	perfstats_done(P);

	/* Close the log file. */
	logging_close(logfile);

	/* Shut down the request queue. */
	wire_requestqueue_destroy(Q_t);
	wire_requestqueue_free(Q_t);

	/* Close sockets. */
	if (close(sock_t))
		warnp("close");
	if (close(sock_s))
		warnp("close");

	/* Free socket addresses. */
	sock_addr_freelist(sas_t);
	sock_addr_freelist(sas_s);

	/* Free option strings. */
	free(opt_t);
	free(opt_s);
	free(opt_p);
	free(opt_l);

	/* Success! */
	return (0);
}

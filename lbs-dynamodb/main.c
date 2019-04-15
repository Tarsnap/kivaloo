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
#include "wire.h"

#include "deleteto.h"
#include "dispatch.h"
#include "metadata.h"
#include "state.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-lbs-dynamodb-kv -s <lbs socket>"
	    " -t <dynamodb-kv socket> -b <item size> [-1] [-p <pidfile>]\n");
	fprintf(stderr, "       kivaloo-lbs-dynamodb-kv --version\n");
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
	struct wire_requestqueue * Q_DDBKV;
	struct metadata * M;
	struct deleteto * deleter;
	struct state * S;
	struct dispatch_state * D;
	int s;
	int s_t;

	/* Command-line parameters. */
	char * opt_s = NULL;
	char * opt_t = NULL;
	size_t opt_b = 0;
	char * opt_p = NULL;
	int opt_1 = 0;

	/* Working variables. */
	struct sock_addr ** sas_s;
	struct sock_addr ** sas_t;
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-b"):
			if (opt_b != 0)
				usage();
			if (PARSENUM(&opt_b, optarg, 512, 8192))
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
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-lbs-dynamodb-kv @VERSION@\n");
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

	/* Sanity-check options. */
	if (opt_s == NULL)
		usage();
	if (opt_t == NULL)
		usage();
	if (opt_b == 0)
		usage();
	if (opt_b % 1024) {
		warn0("DynamoDB item size is unlikely to be optimal: %zu",
		    opt_b);
	}

	/* Resolve the listening address. */
	if ((sas_s = sock_resolve(opt_s)) == NULL) {
		warnp("Error resolving socket address: %s", opt_s);
		exit(1);
	}
	if (sas_s[0] == NULL) {
		warn0("No addresses found for %s", opt_s);
		exit(1);
	}

	/* Resolve the target (dynamodb-kv daemon) address. */
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
	if ((s = sock_listener(sas_s[0])) == -1)
		exit(1);

	/* Connect to the dynamodb-kv daemon. */
	if ((s_t = sock_connect(sas_t)) == -1)
		exit(1);

	/* Create a queue of requests to the dynamodb-kv daemon. */
	if ((Q_DDBKV = wire_requestqueue_init(s_t)) == NULL) {
		warnp("Cannot create DynamoDB-KV request queue");
		exit(1);
	}

	/* Create a metadata handler. */
	if ((M = metadata_init(Q_DDBKV)) == NULL) {
		warnp("Error initializing state metadata handler");
		exit(1);
	}

	/* Create a deleter. */
	if ((deleter = deleteto_init(Q_DDBKV, M)) == NULL) {
		warnp("Error initializing garbage collection");
		exit(1);
	}

	/* Initialize the internal state. */
	if ((S = state_init(Q_DDBKV, opt_b, M)) == NULL) {
		warnp("Error initializing state from DynamoDB");
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

	/* Handle connections, one at once. */
	do {
		/* Accept a connection. */
		if ((D = dispatch_accept(S, deleter, s)) == NULL)
			exit(1);

		/* Loop until the connection dies. */
		do {
			if (events_run()) {
				warnp("Error running event loop");
				exit(1);
			}
		} while (dispatch_alive(D));

		/* Close and free the connection. */
		if (dispatch_done(D))
			exit(1);
	} while (opt_1 == 0);

	/* Clean up the internal state. */
	state_free(S);

	/*
	 * Shut down deleting (cleanly if possible, but we don't care if we
	 * encounter an error at this point).
	 */
	deleteto_stop(deleter);

	/* Shut down the metadata handler. */
	metadata_free(M);

	/* Shut down the dynamodb-kv request queue. */
	wire_requestqueue_destroy(Q_DDBKV);
	wire_requestqueue_free(Q_DDBKV);

	/* Close sockets. */
	close(s_t);
	close(s);

	/* Free socket addresses. */
	sock_addr_freelist(sas_t);
	sock_addr_freelist(sas_s);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Free option strings. */
	free(opt_s);
	free(opt_p);
	free(opt_t);

	/* Success! */
	exit(0);
}

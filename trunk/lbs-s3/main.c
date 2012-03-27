#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "events.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#include "deleteto.h"
#include "dispatch.h"
#include "s3state.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-lbs-s3 -s <lbs socket> -t <s3 socket> "
	    "-b <block size> -B <S3 bucket> [-1] [-p <pidfile>]\n");
	exit(1);
}

/* Macro to simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: -%c %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{
	/* State variables. */
	struct wire_requestqueue * Q_S3;
	struct deleteto * deleter;
	struct s3state * S;
	struct dispatch_state * D;
	int s;
	int s_t;

	/* Command-line parameters. */
	char * opt_s = NULL;
	char * opt_t = NULL;
	intmax_t opt_b = -1;
	char * opt_B = NULL;
	char * opt_p = NULL;
	int opt_1 = 0;

	/* Working variables. */
	struct sock_addr ** sas_s;
	struct sock_addr ** sas_t;
	int ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = getopt(argc, argv, "B:b:p:s:t:1")) != -1) {
		switch (ch) {
		case 'B':
			if (opt_B != NULL)
				usage();
			if ((opt_B = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		case 'b':
			if (opt_b != -1)
				usage();
			opt_b = strtoimax(optarg, NULL, 0);
			break;
		case 'p':
			if (opt_p != NULL)
				usage();
			if ((opt_p = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		case 's':
			if (opt_s != NULL)
				usage();
			if ((opt_s = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		case 't':
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		case '1':
			if (opt_1 != 0)
				usage();
			opt_1 = 1;
			break;
		default:
			usage();
		}
	}

	/* We should have processed all the arguments. */
	if (argc != optind)
		usage();

	/* Sanity-check options. */
	if (opt_s == NULL)
		usage();
	if (opt_t == NULL)
		usage();
	if (opt_B == NULL)
		usage();
	if (opt_b == -1)
		usage();
	if ((opt_b < 512) || (opt_b > 128 * 1024)) {
		warn0("Block size must be in [2^9, 2^17]");
		exit(1);
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

	/* Resolve the target (S3 daemon) address. */
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

	/* Connect to the S3 daemon. */
	if ((s_t = sock_connect(sas_t)) == -1)
		exit(1);

	/* Create a queue of requests to the S3 daemon. */
	if ((Q_S3 = wire_requestqueue_init(s_t)) == NULL) {
		warnp("Cannot create S3 request queue");
		exit(1);
	}

	/* Create a deleter state. */
	if ((deleter = deleteto_init(Q_S3, opt_B)) == NULL) {
		warnp("Error initializing garbage collection for"
		    " S3 bucket: %s", opt_B);
		exit(1);
	}

	/* Initialize the S3 state. */
	if ((S = s3state_init(Q_S3, opt_B, opt_b, deleter)) == NULL) {
		warnp("Error initializing from S3 bucket: %s", opt_B);
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
		if ((D = dispatch_accept(S, s)) == NULL)
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

	/* Clean up the S3 state. */
	s3state_free(S);

	/*
	 * Shut down deleting (cleanly if possible, but we don't care if we
	 * encounter an error at this point).
	 */
	deleteto_stop(deleter);

	/* Shut down the S3 request queue. */
	wire_requestqueue_destroy(Q_S3);
	wire_requestqueue_free(Q_S3);

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
	free(opt_B);

	/* Success! */
	return (0);
}

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "aws_readkeys.h"
#include "daemonize.h"
#include "events.h"
#include "getopt.h"
#include "insecure_memzero.h"
#include "logging.h"
#include "parsenum.h"
#include "s3_request_queue.h"
#include "sock.h"
#include "warnp.h"

#include "dispatch.h"
#include "dns.h"

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-s3 -s <s3 socket> -r <s3 region> "
	    "-k <keyfile> [-l <logfile>] [-n <max # connections>] [-1] "
	    "[-p <pidfile>]\n");
	fprintf(stderr, "       kivaloo-s3 --version\n");
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
	struct s3_request_queue * Q;
	struct dns_reader * DR;
	struct dispatch_state * D;
	int s;

	/* Command-line parameters. */
	char * opt_k = NULL;
	char * opt_l = NULL;
	char * opt_p = NULL;
	size_t opt_n = 16;
	char * opt_r = NULL;
	char * opt_s = NULL;
	int opt_1 = 0;

	/* Working variable. */
	char * s3_key_id;
	char * s3_key_secret;
	struct sock_addr ** sas;
	char * s3_host;
	struct logging_file * logfile;
	size_t i;
	const char * ch;

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-k"):
			if (opt_k != NULL)
				usage();
			if ((opt_k = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-l"):
			if (opt_l != NULL)
				usage();
			if ((opt_l = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-n"):
			if (opt_n != 16)
				usage();
			if (PARSENUM(&opt_n, optarg, 1, 250)) {
				warn0("Maximum number of connections must"
				    " be in [1, 250]");
				exit(1);
			}
			break;
		GETOPT_OPTARG("-p"):
			if (opt_p != NULL)
				usage();
			if ((opt_p = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-r"):
			if (opt_r != NULL)
				usage();
			if ((opt_r = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-s"):
			if (opt_s != NULL)
				usage();
			if ((opt_s = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-s3 @VERSION@\n");
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
	if (opt_k == NULL)
		usage();
	if (opt_r == NULL)
		usage();
	if (opt_s == NULL)
		usage();

	/* Read the key file. */
	if (aws_readkeys(opt_k, &s3_key_id, &s3_key_secret)) {
		warnp("Error reading S3 keys from %s", opt_k);
		exit(1);
	}

	/* Create an S3 request queue. */
	if ((Q = s3_request_queue_init(s3_key_id, s3_key_secret, opt_r,
	    opt_n)) == NULL) {
		warnp("Error creating S3 request queue");
		exit(1);
	}

	/* Construct the S3 endpoint host name. */
	if (asprintf(&s3_host, "s3.%s.amazonaws.com:80", opt_r) == -1) {
		warnp("asprintf");
		exit(1);
	}

	/* Perform an initial DNS lookup for the S3 endpoint. */
	if ((sas = sock_resolve(s3_host)) == NULL) {
		warnp("Error resolving S3 endpoint: %s", s3_host);
		exit(1);
	}

	/* Add addresses to the request queue. */
	for (i = 0; sas[i] != NULL; i++) {
		if (s3_request_queue_addaddr(Q, sas[i], 600)) {
			warnp("Error adding S3 endpoint address");
			exit(1);
		}
	}

	/* Free the initial set of S3 endpoint addresses. */
	sock_addr_freelist(sas);

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

	/* If requested, create a log file. */
	if (opt_l != NULL) {
		if ((logfile = logging_open(opt_l)) == NULL) {
			warnp("Cannot open log file");
			exit(1);
		}
		s3_request_queue_log(Q, logfile);
	} else {
		logfile = NULL;
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

	/* Start DNS lookups. */
	if ((DR = dns_reader_start(Q, s3_host)) == NULL) {
		warnp("Failed to start DNS resolution");
		exit(1);
	}

	/* Handle connections, one at once. */
	do {
		/* accept a connection. */
		if ((D = dispatch_accept(Q, s)) == NULL) {
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
		if (dispatch_done(D))
			exit(1);
	} while (opt_1 == 0);

	/* Stop DNS lookups. */
	dns_reader_stop(DR);

	/* Free the S3 request queue. */
	s3_request_queue_free(Q);

	/* Close the log file, if we have one. */
	if (logfile != NULL)
		logging_close(logfile);

	/* Close the listening socket. */
	if (close(s))
		warnp("close");

	/* Free the address structures. */
	sock_addr_freelist(sas);

	/* Free string allocated by asprintf. */
	free(s3_host);

	/* Free key strings. */
	free(s3_key_id);
	insecure_memzero(s3_key_secret, strlen(s3_key_secret));
	free(s3_key_secret);

	/* Free option strings. */
	free(opt_k);
	free(opt_l);
	free(opt_p);
	free(opt_r);
	free(opt_s);

	/* Success! */
	return (0);
}

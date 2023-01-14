#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "aws_readkeys.h"
#include "daemonize.h"
#include "dynamodb_request_queue.h"
#include "events.h"
#include "getopt.h"
#include "insecure_memzero.h"
#include "logging.h"
#include "serverpool.h"
#include "sock.h"
#include "warnp.h"

#include "capacity.h"
#include "dispatch.h"

static void
usage(void)
{

	fprintf(stderr, "usage: dynamodb-kv %s %s %s %s %s %s %s\n",
	    "-s <dynamodb-kv socket>", "-r <DynamoDB region>",
	    "-t <DynamoDB table>", "-k <keyfile>", "[-1]",
	    "[-l <logfile>]", "[-p <pidfile>]");
	fprintf(stderr, "       dynamodb-kv --version\n");
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
	struct serverpool * SP;
	struct dynamodb_request_queue * QW;
	struct dynamodb_request_queue * QR;
	struct dispatch_state * D;
	int s;

	/* Command-line parameters. */
	char * opt_k = NULL;
	char * opt_l = NULL;
	char * opt_p = NULL;
	char * opt_r = NULL;
	char * opt_s = NULL;
	char * opt_t = NULL;
	int opt_1 = 0;

	/* Working variable. */
	char * dynamodb_host;
	char * key_id;
	char * key_secret;
	struct sock_addr ** sas;
	struct logging_file * logfile;
	struct capacity_reader * M;
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
		GETOPT_OPTARG("-t"):
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "dynamodb-kv @VERSION@\n");
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

	/* Verify that we have mandatory options. */
	if (opt_k == NULL)
		usage();
	if (opt_r == NULL)
		usage();
	if (opt_s == NULL)
		usage();
	if (opt_t == NULL)
		usage();

	/* Construct the DynamoDB endpoint host name. */
	if (asprintf(&dynamodb_host, "dynamodb.%s.amazonaws.com:443",
	    opt_r) == -1) {
		warnp("asprintf");
		exit(1);
	}

	/* Start looking up addresses for DynamoDB endpoints. */
	if ((SP = serverpool_create(dynamodb_host, 15, 120)) == NULL) {
		warnp("Error starting DNS lookups for %s", dynamodb_host);
		exit(1);
	}

	/* Read the key file. */
	if (aws_readkeys(opt_k, &key_id, &key_secret)) {
		warnp("Error reading AWS keys from %s", opt_k);
		exit(1);
	}

	/* Create DynamoDB request queues for writes and reads. */
	if ((QW = dynamodb_request_queue_init(key_id, key_secret,
	    opt_r, SP)) == NULL) {
		warnp("Error creating DynamoDB request queue");
		exit(1);
	}
	if ((QR = dynamodb_request_queue_init(key_id, key_secret,
	    opt_r, SP)) == NULL) {
		warnp("Error creating DynamoDB request queue");
		exit(1);
	}

	/* Start reading table throughput parameters. */
	if ((M = capacity_init(key_id, key_secret, opt_t, opt_r,
	    SP, QW, QR)) == NULL) {
		warnp("Error reading DynamoDB table metadata");
		exit(1);
	}

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
		dynamodb_request_queue_log(QW, logfile);
		dynamodb_request_queue_log(QR, logfile);
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

	/* Handle connections, one at once. */
	do {
		/* accept a connection. */
		if ((D = dispatch_accept(QW, QR, opt_t, s)) == NULL) {
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

	/* Close the log file, if we have one. */
	if (logfile != NULL)
		logging_close(logfile);

	/* Close the listening socket. */
	if (close(s))
		warnp("close");

	/* Free the address structures. */
	sock_addr_freelist(sas);

	/* Stop performing DescribeTable requests. */
	capacity_free(M);

	/* Free DynamoDB request queues. */
	dynamodb_request_queue_free(QR);
	dynamodb_request_queue_free(QW);

	/* Stop DNS lookups. */
	serverpool_free(SP);

	/* Free string allocated by asprintf. */
	free(dynamodb_host);

	/* Free key strings. */
	free(key_id);
	insecure_memzero(key_secret, strlen(key_secret));
	free(key_secret);

	/* Free option strings. */
	free(opt_k);
	free(opt_l);
	free(opt_p);
	free(opt_r);
	free(opt_s);
	free(opt_t);

	/* Success! */
	exit(0);
}

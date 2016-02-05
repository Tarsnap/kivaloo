#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "daemonize.h"
#include "elasticarray.h"
#include "events.h"
#include "getopt.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#include "dispatch.h"

ELASTICARRAY_DECL(ADDRLIST, addrlist, struct sock_addr *);

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-mux -t <target socket> "
	    "-s <source socket> [-s <source socket> ...] "
	    "[-n <max # connections] [-p <pidfile>]\n");
	fprintf(stderr, "       kivaloo-mux --version\n");
	exit(1);
}

/* Simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: -%c %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{
	/* State variables. */
	int * socks_s;
	int sock_t;
	struct wire_requestqueue * Q_t;
	struct dispatch_state * dstate;

	/* Command-line parameters. */
	intmax_t opt_n = 0;
	char * opt_p = NULL;
	char * opt_t = NULL;
	ADDRLIST opt_s;
	char * opt_s_1 = NULL;

	/* Working variables. */
	size_t opt_s_size;
	struct sock_addr ** sas;
	size_t i;
	const char * ch;

	WARNP_INIT;

	/* We have no addresses to listen on yet. */
	if ((opt_s = addrlist_init(0)) == NULL) {
		warnp("addrlist_init");
		exit(1);
	}

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-n"):
			if (opt_n != 0)
				usage();
			if ((opt_n = strtoimax(optarg, NULL, 0)) == 0) {
				warn0("Invalid option: -n %s", optarg);
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
			/* Keep a copy of the path for pidfile generation. */
			if ((opt_s_1 == NULL) &&
			    ((opt_s_1 = strdup(optarg)) == NULL))
				OPT_EPARSE(ch, optarg);

			/* Attempt to resolve to a list of addresses. */
			if ((sas = sock_resolve(optarg)) == NULL) {
				warnp("Cannot resolve address: %s", optarg);
				exit(1);
			}
			if (sas[0] == NULL) {
				warn0("No addresses found for %s", optarg);
				exit(1);
			}

			/* Push pointers to addresses onto the list. */
			for (i = 0; sas[i] != NULL; i++) {
				if (addrlist_append(opt_s, &sas[i], 1))
					OPT_EPARSE(ch, optarg);
			}

			/* Free the array (but keep the addresses). */
			free(sas);
			break;
		GETOPT_OPTARG("-t"):
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-mux @VERSION@\n");
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s\n", ch);
			/* FALLTHROUGH */
		GETOPT_DEFAULT:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 0)
		usage();

	/* Sanity-check options. */
	if ((opt_n < 0) || (opt_n > 65535))
		usage();
	if ((opt_s_size = addrlist_getsize(opt_s)) == 0)
		usage();
	if (opt_t == NULL)
		usage();

	/* Resolve target address. */
	if ((sas = sock_resolve(opt_t)) == NULL) {
		warnp("Error resolving socket address: %s", opt_t);
		exit(1);
	}
	if (sas[0] == NULL) {
		warn0("No addresses found for %s", opt_t);
		exit(1);
	}

	/* Connect to the target. */
	if ((sock_t = sock_connect(sas)) == -1)
		exit(1);

	/* Free the target address(es). */
	sock_addr_freelist(sas);

	/* Create a queue of requests to the target. */
	if ((Q_t = wire_requestqueue_init(sock_t)) == NULL) {
		warnp("Cannot create request queue");
		exit(1);
	}

	/* Allocate array of source sockets. */
	if ((socks_s = malloc(opt_s_size * sizeof(int))) == NULL) {
		warnp("malloc");
		exit(1);
	}

	/* Create listening sockets. */
	for (i = 0; i < opt_s_size; i++) {
		if ((socks_s[i] =
		    sock_listener(*addrlist_get(opt_s, i))) == -1)
			exit(1);
	}

	/* Initialize the dispatcher. */
	if ((dstate = dispatch_init(socks_s, opt_s_size,
	    Q_t, opt_n ? (size_t)opt_n : SIZE_MAX)) == NULL) {
		warnp("Failed to initialize dispatcher");
		exit(1);
	}

	/* Daemonize and write pid. */
	if (opt_p == NULL) {
		if (asprintf(&opt_p, "%s.pid", opt_s_1) == -1) {
			warnp("asprintf");
			exit(1);
		}
	}
	if (daemonize(opt_p)) {
		warnp("Failed to daemonize");
		exit(1);
	}

	/* Loop until the dispatcher is finished. */
	do {
		if (events_run()) {
			warnp("Error running event loop");
			exit(1);
		}
	} while (dispatch_alive(dstate));

	/* Clean up the dispatcher. */
	dispatch_done(dstate);

	/* Shut down the request queue. */
	wire_requestqueue_destroy(Q_t);
	wire_requestqueue_free(Q_t);

	/* Close sockets. */
	for (i = 0; i < opt_s_size; i++)
		close(socks_s[i]);
	free(socks_s);
	close(sock_t);

	/* Free source socket addresses. */
	for (i = 0; i < addrlist_getsize(opt_s); i++)
		sock_addr_free(*addrlist_get(opt_s, i));
	addrlist_free(opt_s);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Free option strings. */
	free(opt_p);
	free(opt_s_1);
	free(opt_t);

	/* Success! */
	return (0);
}

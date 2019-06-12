#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "asprintf.h"
#include "events.h"
#include "sock.h"
#include "warnp.h"

#include "network_ssl.h"

static int conndone = 0;

static int
callback_write_test(void * cookie, ssize_t val)
{

	(void)cookie;

	/* We should have written a single byte, or received an error. */
	assert((val == 1) || (val == -1));

	/* An error occurred. */
	if (val == -1)
		return (-1);

	/* We can stop the event loop peacefully. */
	conndone = 1;

	/* Success! */
	return (0);
}

static int
do_request(struct network_ssl_ctx * ctx)
{
	const uint8_t req = 0;

	/* Send a 1-byte buffer containing 0. */
	if (network_ssl_write(ctx, &req, 1, 1, callback_write_test, NULL)
	    == NULL) {
		warn0("network_ssl_write");
		goto err0;
	}

	/* Wait for reply. */
	if (events_spin(&conndone))
		goto err1;

	/* Clean up. */
	events_shutdown();

	/* Success! */
	return (0);

err1:
	events_shutdown();
err0:
	/* Failure !*/
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct network_ssl_ctx * ctx;
	const char * hostname;
	char * sock_addr;

	/* Working variables. */
	struct sock_addr ** sas_t;
	int socket;
	const char * certfile;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_network_ssl %s\n",
		    "<hostname>");
		goto err0;
	}
	hostname = argv[1];

	/* Get default certfile, and load it. */
	if ((certfile = network_ssl_default_certfile()) == NULL) {
		warn0("Need a certfile");
		goto err0;
	}
	if (network_ssl_loadroot(certfile)) {
		warn0("Failed to initialize SSL layer");
		goto err0;
	}

	/* Create target address. */
	if (asprintf(&sock_addr, "%s:443", hostname) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Resolve target address. */
	if ((sas_t = sock_resolve(sock_addr)) == NULL) {
		warnp("Error resolving socket address: %s", sock_addr);
		goto err1;
	}
	if (sas_t[0] == NULL) {
		warn0("No address found for %s", sock_addr);
		goto err2;
	}

	/* Connect to host. */
	if ((socket = sock_connect(sas_t)) == -1) {
		warnp("sock_connect");
		goto err2;
	}
	if ((ctx = network_ssl_open(socket, hostname)) == NULL) {
		warn0("network_ssl_open");
		goto err2;
	}

	/* Make request. */
	if (do_request(ctx) == -1) {
		warn0("Request failed.");
		goto err2;
	}

	/* Clean up. */
	network_ssl_close(ctx);
	close(socket);
	sock_addr_freelist(sas_t);
	free(sock_addr);

	/* Success! */
	exit(0);

err2:
	sock_addr_freelist(sas_t);
err1:
	free(sock_addr);
err0:
	/* Failure! */
	exit(1);
}

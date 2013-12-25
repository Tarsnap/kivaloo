#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "proto_s3.h"
#include "sock.h"
#include "wire.h"
#include "warnp.h"

struct put_state {
	int done;
	int failed;
};

static int
callback_done(void * cookie, int failed)
{
	struct put_state * C = cookie;

	C->done = 1;
	C->failed = failed;

	return (0);
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;
	struct stat sb;
	FILE * f;
	uint8_t * buf;
	struct put_state C;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 5) {
		fprintf(stderr, "usage: s3_put %s %s %s %s\n", "<socketname>",
		    "<file>", "<bucket>", "<object>");
		exit(1);
	}

	/* Stat file. */
	if (stat(argv[2], &sb)) {
		warnp("stat(%s)", argv[2]);
		exit(1);
	}
	if (!S_ISREG(sb.st_mode) ||
	    sb.st_size > PROTO_S3_MAXLEN || sb.st_size == 0) {
		warn0("Bad file: %s", argv[2]);
		exit(1);
	}
	if ((buf = malloc(sb.st_size)) == NULL) {
		warnp("malloc");
		exit(1);
	}

	/* Resolve the socket address and connect. */
	if ((sas = sock_resolve(argv[1])) == NULL) {
		warnp("Error resolving socket address: %s", argv[1]);
		exit(1);
	}
	if (sas[0] == NULL) {
		warn0("No addresses found for %s", argv[1]);
		exit(1);
	}
	if ((s = sock_connect(sas)) == -1)
		exit(1);

	/* Create a request queue. */
	if ((Q = wire_requestqueue_init(s)) == NULL) {
		warnp("Cannot create packet write queue");
		exit(1);
	}

	/* Read file. */
	if ((f = fopen(argv[2], "r")) == NULL) {
		warnp("fopen(%s)", argv[2]);
		exit(1);
	}
	if (fread(buf, sb.st_size, 1, f) != 1) {
		warnp("fread(%s)", argv[2]);
		exit(1);
	}
	if (fclose(f)) {
		warnp("fclose(%s)", argv[2]);
		exit(1);
	}

	/* Send request. */
	C.done = 0;
	if (proto_s3_request_put(Q, argv[3], argv[4], sb.st_size, buf,
	    callback_done, &C)) {
		warnp("proto_s3_request_put");
		exit(1);
	}
	if (events_spin(&C.done)) {
		warnp("events_spin");
		exit(1);
	}

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Success! */
	exit(0);
}

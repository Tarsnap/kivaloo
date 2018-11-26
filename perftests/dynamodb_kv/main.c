#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "proto_dynamodb_kv.h"
#include "sock.h"
#include "wire.h"
#include "warnp.h"

struct status {
	int done;
	int status;
};

static int
callback_status(void * cookie, int status)
{
	struct status * C = cookie;

	C->done = 1;
	C->status = status;

	return (0);
}

static int
callback_get(void * cookie, int status, const uint8_t * buf, size_t buflen)
{
	struct status * C = cookie;

	C->done = 1;
	C->status = status;
	if (status == 0) {
		fprintf(stderr, "value returned: \"");
		fwrite(buf, buflen, 1, stderr);
		fprintf(stderr, "\"\n");
	} else if (status == 2) {
		fprintf(stderr, "no value associated\n");
	}

	return (0);
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;
	struct status C;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: dynamodb_kv %s\n",
		    "<socketname>");
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

	/* Send DELETE request. */
	C.done = 0;
	fprintf(stderr, "DELETE \"key\"\n");
	if (proto_dynamodb_kv_request_delete(Q, "key", callback_status, &C)) {
		warnp("proto_dynamodb_kv_request_delete");
		exit(1);
	}
	if (events_spin(&C.done)) {
		warnp("events_spin");
		exit(1);
	}
	if (C.status) {
		warn0("DELETE returned status %d", C.status);
		exit(1);
	}

	/* Send Consistent GET request. */
	C.done = 0;
	fprintf(stderr, "GET \"key\"\n");
	if (proto_dynamodb_kv_request_getc(Q, "key", callback_get, &C)) {
		warnp("proto_dynamodb_kv_request_getc");
		exit(1);
	}
	if (events_spin(&C.done)) {
		warnp("events_spin");
		exit(1);
	}
	if (C.status != 2) {
		warn0("Consistent GET returned status %d", C.status);
		exit(1);
	}

	/* Send PUT request. */
	C.done = 0;
	fprintf(stderr, "PUT \"key\" = \"value\"\n");
	if (proto_dynamodb_kv_request_put(Q, "key",
	    (const uint8_t *)"value", 5, callback_status, &C)) {
		warnp("proto_dynamodb_kv_request_put");
		exit(1);
	}
	if (events_spin(&C.done)) {
		warnp("events_spin");
		exit(1);
	}
	if (C.status) {
		warn0("PUT returned status %d", C.status);
		exit(1);
	}

	/* Send GET request. */
	C.done = 0;
	fprintf(stderr, "GET \"key\"\n");
	if (proto_dynamodb_kv_request_get(Q, "key", callback_get, &C)) {
		warnp("proto_dynamodb_kv_request_get");
		exit(1);
	}
	if (events_spin(&C.done)) {
		warnp("events_spin");
		exit(1);
	}
	if (C.status) {
		warn0("GET returned status %d", C.status);
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

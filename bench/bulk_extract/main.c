#include <sys/time.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kvldskey.h"
#include "monoclock.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

struct bulkextract_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	struct kvldskey * nullkey;
	int failed;
	int done;

	/* Bits needed for measuring performance. */
	struct timeval tv_60;
	struct timeval tv_50;
	uint64_t N;
};

static int startrange(struct bulkextract_state *);

static int
callback_done(void * cookie, int failed)
{
	struct bulkextract_state * C = cookie;

	/* Did we fail? */
	if (failed) {
		C->failed = 1;
		C->done = 1;
	}

	/* Restart the RANGE requests. */
	return (startrange(C));
}

static int
callback_range(void * cookie,
    const struct kvldskey * key, const struct kvldskey * value)
{
	struct bulkextract_state * C = cookie;
	struct timeval tv;

	(void)key; /* UNUSED */
	(void)value; /* UNUSED */

	/* Read the current time. */
	if (monoclock_get(&tv)) {
		warnp("Error reading clock");
		goto err0;
	}

	/* Are we finished?  Are we within the 50-60 second range? */
	if ((tv.tv_sec > C->tv_60.tv_sec) ||
	    ((tv.tv_sec == C->tv_60.tv_sec) &&
		(tv.tv_usec > C->tv_60.tv_usec))) {
		C->done = 1;
	} else if ((tv.tv_sec > C->tv_50.tv_sec) ||
	    ((tv.tv_sec == C->tv_50.tv_sec) &&
		(tv.tv_usec > C->tv_50.tv_usec))) {
		C->N += 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
startrange(struct bulkextract_state * C)
{

	/* Start issuing RANGE requests. */
	return (proto_kvlds_request_range2(C->Q, C->nullkey, C->nullkey,
	    callback_range, callback_done, C));
}

static int
bulkextract(struct wire_requestqueue * Q)
{
	struct bulkextract_state C;
	struct timeval tv_now;

	/* Initialize state. */
	C.Q = Q;
	C.failed = C.done = 0;
	C.N = 0;

	/* Create null key (used as start and end of range). */
	if ((C.nullkey = kvldskey_create(NULL, 0)) == NULL)
		goto err0;

	/* Get current time and store T+60s and T+50s. */
	if (monoclock_get(&tv_now)) {
		warnp("Error reading clock");
		goto err1;
	}
	C.tv_60.tv_sec = tv_now.tv_sec + 60;
	C.tv_60.tv_usec = tv_now.tv_usec;
	C.tv_50.tv_sec = tv_now.tv_sec + 50;
	C.tv_50.tv_usec = tv_now.tv_usec;

	/* Launch the first RANGE request. */
	if (startrange(&C))
		goto err1;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("RANGE request failed");
		goto err1;
	}

	/* Print number of pairs read in a single second. */
	printf("%" PRIu64 "\n", C.N / 10);

	/* Clean up. */
	kvldskey_free(C.nullkey);

	/* Success! */
	return (0);

err1:
	kvldskey_free(C.nullkey);
err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: bulk_insert %s\n", "<socketname>");
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

	/* Start RANGE request(s). */
	if (bulkextract(Q))
		exit(1);

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

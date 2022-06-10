#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "events.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#define BENCHMARK_START 50	/* Seconds before starting to record. */
#define BENCHMARK_SECONDS 10	/* Seconds to record. */

struct bulkextract_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	struct kvldskey * nullkey;
	int failed;
	int done;

	/* Bits needed for measuring performance. */
	struct bench * B;
};

static int startrange(struct bulkextract_state *);

static int
callback_done(void * cookie, int failed)
{
	struct bulkextract_state * C = cookie;
	int rc = 0;

	/* Did we fail? */
	if (failed) {
		C->failed = 1;
		C->done = 1;
	}

	/* Restart the RANGE requests. */
	if (!C->done)
		rc = startrange(C);

	return (rc);
}

static int
callback_range(void * cookie,
    const struct kvldskey * key, const struct kvldskey * value)
{
	struct bulkextract_state * C = cookie;

	(void)key; /* UNUSED */
	(void)value; /* UNUSED */

	/* Notify the benchmarking code, and check if we should quit. */
	if (bench_tick(C->B, &C->done)) {
		warnp("bench_tick");
		goto err0;
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

	/* Initialize state. */
	C.Q = Q;
	C.failed = C.done = 0;

	/* Create null key (used as start and end of range). */
	if ((C.nullkey = kvldskey_create(NULL, 0)) == NULL)
		goto err0;

	/* Prepare benchmark time handling. */
	if ((C.B = bench_init(BENCHMARK_START, BENCHMARK_SECONDS)) == NULL) {
		warn0("bench_init");
		goto err1;
	}

	/* Launch the first RANGE request. */
	if (startrange(&C))
		goto err2;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("RANGE request failed");
		goto err2;
	}

	/* Print median number of pairs read in a single second. */
	printf("%" PRIu64 "\n", bench_median(C.B));

	/* Clean up. */
	bench_free(C.B);
	kvldskey_free(C.nullkey);

	/* Success! */
	return (0);

err2:
	bench_free(C.B);
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

	/* Success! */
	exit(0);
}

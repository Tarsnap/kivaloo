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

struct bulkupdate_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	FILE * f;
	size_t Nip;
	int generation;
	int failed;
	int done;

	/* Temporary key and value structures. */
	struct kvldskey * key;
	struct kvldskey * val;

	/* Bits needed for measuring performance. */
	struct bench * B;
};

static int callback_done(void *, int);

static int
sendbatch(struct bulkupdate_state * C)
{

	while (C->Nip < 4096) {
		/* Try to read a key-value pair. */
		if (fread(C->key->buf, 40, 1, C->f) < 1) {
			C->generation++;
			rewind(C->f);
			continue;
		}
		if (fread(C->val->buf, 40, 1, C->f) < 1) {
			C->generation++;
			rewind(C->f);
			continue;
		}

		/* Create a new (somewhat arbitrary) value to record. */
		C->val->buf[39] += C->generation;

		/* Send the request. */
		if (proto_kvlds_request_set(C->Q, C->key, C->val,
		    callback_done, C))
			goto err0;
		C->Nip += 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_done(void * cookie, int failed)
{
	struct bulkupdate_state * C = cookie;

	/* This request is no longer in progress. */
	C->Nip -= 1;

	/* Did we fail? */
	if (failed) {
		C->done = 1;
		C->failed = 1;
	}

	/* Notify the benchmarking code, and check if we should quit. */
	if (bench_tick(C->B, &C->done)) {
		warnp("bench_tick");
		goto err0;
	}

	/* Send more requests if possible. */
	if ((!C->done) && sendbatch(C))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
bulkupdate(struct wire_requestqueue * Q, FILE * f)
{
	struct bulkupdate_state C;
	uint8_t buf[40];	/* dummy */

	/* Initialize. */
	C.Q = Q;
	C.f = f;
	C.Nip = 0;
	C.generation = 0;
	C.failed = 0;
	C.done = 0;

	/* Allocate key and value structures. */
	memset(buf, 0, 40);
	if ((C.key = kvldskey_create(buf, 40)) == NULL)
		goto err0;
	if ((C.val = kvldskey_create(buf, 40)) == NULL)
		goto err1;

	/* Prepare benchmark time handling. */
	if ((C.B = bench_init(BENCHMARK_START, BENCHMARK_SECONDS)) == NULL) {
		warn0("bench_init");
		goto err2;
	}

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		goto err3;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		goto err3;
	}

	/* Print median number of updates performed in a single second. */
	printf("%" PRIu64 "\n", bench_median(C.B));

	/* Free the key and value structures. */
	bench_free(C.B);
	kvldskey_free(C.val);
	kvldskey_free(C.key);

	/* Success! */
	return (0);

err3:
	bench_free(C.B);
err2:
	kvldskey_free(C.val);
err1:
	kvldskey_free(C.key);
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
		fprintf(stderr, "usage: bulk_update %s\n", "<socketname>");
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

	/* Start bulk updating. */
	if (bulkupdate(Q, stdin))
		exit(1);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Success! */
	exit(0);
}

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "events.h"
#include "kvldskey.h"
#include "mkpair.h"
#include "parsenum.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

#define BENCHMARK_START 50	/* Seconds before starting to record. */
#define BENCHMARK_SECONDS 100	/* Seconds to record. */

struct randomread_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	FILE * f;
	size_t Nip;
	uint64_t Nmax;
	int failed;
	int done;

	/* Temporary key structure. */
	struct kvldskey * key;

	/* Bits needed for measuring performance. */
	struct bench * B;
};

static int callback_get(void *, int, struct kvldskey *);

static int
sendbatch(struct randomread_state * C)
{
	unsigned long N;
	uint64_t X, Y;

	while (C->Nip < 4096) {
		/* Generate a random key. */
		N = (size_t)random() % C->Nmax;
		X = N >> 16;
		Y = N - (X << 16);
		mkkey(X, Y, C->key->buf);

		/* Send the request. */
		if (proto_kvlds_request_get(C->Q, C->key,
		    callback_get, C))
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
callback_get(void * cookie, int failed, struct kvldskey * value)
{
	struct randomread_state * C = cookie;

	/* This request is no longer in progress. */
	C->Nip -= 1;

	/* Did we fail? */
	if (failed) {
		C->done = 1;
		C->failed = 1;
	}

	/* If we have a value, free it. */
	if (failed == 0)
		kvldskey_free(value);

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
randomread(struct wire_requestqueue * Q, uint64_t N)
{
	struct randomread_state C;
	uint8_t buf[40];	/* dummy */

	/* Initialize. */
	C.Q = Q;
	C.Nip = 0;
	C.Nmax = N;
	C.failed = 0;
	C.done = 0;

	/* Allocate key structure. */
	memset(buf, 0, 40);
	if ((C.key = kvldskey_create(buf, 40)) == NULL)
		goto err0;

	/* Prepare benchmark time handling. */
	if ((C.B = bench_init(BENCHMARK_START, BENCHMARK_SECONDS)) == NULL) {
		warn0("bench_init");
		goto err1;
	}

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		goto err2;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		goto err2;
	}

	/* Print median number of reads performed in a single second. */
	printf("%" PRIu64 "\n", bench_median(C.B));

	/* Free the key structure. */
	bench_free(C.B);
	kvldskey_free(C.key);

	/* Success! */
	return (0);

err2:
	bench_free(C.B);
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
	uintmax_t N;
	int s;
	struct wire_requestqueue * Q;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 3) {
		fprintf(stderr, "usage: random_read %s N\n", "<socketname>");
		exit(1);
	}

	/* Parse N. */
	if (PARSENUM(&N, argv[2])) {
		warnp("Invalid value for N: %s", argv[2]);
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

	/* Start issuing random read requests. */
	if (randomread(Q, N))
		exit(1);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Success! */
	exit(0);
}

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

struct bulkinsert_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	FILE * f;
	size_t Nip;
	int failed;
	int done;

	/* Requests-per-second state. */
	size_t Ndone;
	size_t Ndone_saved;
	struct timeval tv_saved;

	/* Temporary key and value structures. */
	struct kvldskey * key;
	struct kvldskey * val;
};

static int callback_done(void *, int);

static int
sendbatch(struct bulkinsert_state * C)
{

	while (C->Nip < 4096) {
		/* Try to read a key-value pair. */
		if (fread(C->key->buf, 40, 1, C->f) < 1)
			break;
		if (fread(C->val->buf, 40, 1, C->f) < 1)
			break;

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

static void
printperf(struct bulkinsert_state * C)
{
	struct timeval tv_now;
	double T;
	uint64_t N;

	/* Get current time. */
	if (monoclock_get(&tv_now)) {
		warnp("Error reading clock");
		return;
	}

	/* Compute time difference. */
	T = timeval_diff(C->tv_saved, tv_now);

	/* Compute number of requests between then and now. */
	N = C->Ndone - C->Ndone_saved;

	/* Everything completed before now was before 10 s. */
	if (T > 10.0) {
		T = 10.0;
		N -= 1;
	}

	/* Avoid microsecond precision rounding resulting in divide-by-0. */
	if (T < 0.000001)
		T = 0.000001;

	/*
	 * Print requests per second.  Skip this if we handled less than 4096
	 * requests, since that could be a burst of responses from a single
	 * bundle.
	 */
	if (N >= 4096)
		printf("%zu %.0f\n", C->Ndone_saved, N / T);

	/* We've printed this performance point. */
	C->Ndone_saved = 0;
}

static int
callback_done(void * cookie, int failed)
{
	struct bulkinsert_state * C = cookie;
	struct timeval tv_now;

	/* This request is no longer in progress. */
	C->Nip -= 1;

	/* This request is done. */
	C->Ndone += 1;

	/* Did we fail? */
	if (failed) {
		C->done = 1;
		C->failed = 1;
	}

	/* Store new power-of-two timestamp? */
	if ((C->Ndone & (C->Ndone - 1)) == 0) {
		/* If we have a saved timestamp, print results. */
		if (C->Ndone_saved)
			printperf(C);

		/* Store new timestamp. */
		C->Ndone_saved = C->Ndone;
		if (monoclock_get(&C->tv_saved)) {
			warnp("Error reading clock");
			goto err0;
		}
	}

	/* Has it been 1s since the stored timestamp? */
	if (C->Ndone_saved) {
		if (monoclock_get(&tv_now)) {
			warnp("Error reading clock");
			goto err0;
		}
		if ((tv_now.tv_sec >= C->tv_saved.tv_sec + 10) &&
		    ((tv_now.tv_sec > C->tv_saved.tv_sec + 10) ||
		     (tv_now.tv_usec > C->tv_saved.tv_usec)))
			printperf(C);
	}

	/* Send more requests if possible. */
	if (sendbatch(C))
		goto err0;

	/* Are we done? */
	if (C->Nip == 0)
		C->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
bulkinsert(struct wire_requestqueue * Q, FILE * f)
{
	struct bulkinsert_state C;
	uint8_t buf[40];	/* dummy */

	/* Initialize. */
	C.Q = Q;
	C.f = f;
	C.Nip = 0;
	C.failed = 0;
	C.done = 0;
	C.Ndone = 0;
	C.Ndone_saved = 0;

	/* Allocate key and value structures. */
	if ((C.key = kvldskey_create(buf, 40)) == NULL)
		goto err0;
	if ((C.val = kvldskey_create(buf, 40)) == NULL)
		goto err1;

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		goto err2;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		goto err2;
	}

	/* Free the key and value structures. */
	kvldskey_free(C.val);
	kvldskey_free(C.key);

	/* Success! */
	return (0);

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

	/* Start bulk inserting. */
	if (bulkinsert(Q, stdin))
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

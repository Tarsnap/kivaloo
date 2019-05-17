#include <sys/time.h>

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kvldskey.h"
#include "mkpair.h"
#include "monoclock.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

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
	struct timeval tv_150;
	struct timeval tv_50;
	uint64_t N;
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
	struct timeval tv;

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

	/* Read the current time. */
	if (monoclock_get(&tv)) {
		warnp("Error reading clock");
		goto err0;
	}

	/* Are we finished?  Are we within the 50-150 second range? */
	if ((tv.tv_sec > C->tv_150.tv_sec) ||
	    ((tv.tv_sec == C->tv_150.tv_sec) &&
		(tv.tv_usec > C->tv_150.tv_usec))) {
		C->done = 1;
	} else if ((tv.tv_sec > C->tv_50.tv_sec) ||
	    ((tv.tv_sec == C->tv_50.tv_sec) &&
		(tv.tv_usec > C->tv_50.tv_usec))) {
		C->N += 1;
	}

	/* Send more requests if possible. */
	if (sendbatch(C))
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
	struct timeval tv_now;
	uint8_t buf[40];	/* dummy */

	/* Initialize. */
	C.Q = Q;
	C.Nip = 0;
	C.Nmax = N;
	C.failed = 0;
	C.done = 0;
	C.N = 0;

	/* Allocate key structure. */
	if ((C.key = kvldskey_create(buf, 40)) == NULL)
		goto err0;

	/* Get current time and store T+150s and T+50s. */
	if (monoclock_get(&tv_now)) {
		warnp("Error reading clock");
		goto err1;
	}
	C.tv_150.tv_sec = tv_now.tv_sec + 150;
	C.tv_150.tv_usec = tv_now.tv_usec;
	C.tv_50.tv_sec = tv_now.tv_sec + 50;
	C.tv_50.tv_usec = tv_now.tv_usec;

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		goto err1;

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		goto err1;
	}

	/* Print number of reads performed in a single second. */
	printf("%" PRIu64 "\n", C.N / 100);

	/* Free the key structure. */
	kvldskey_free(C.key);

	/* Success! */
	return (0);

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
	if ((N = strtoumax(argv[2], NULL, 0)) == 0) {
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

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Success! */
	exit(0);
}

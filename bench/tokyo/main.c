#include <sys/time.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "events.h"
#include "kvldskey.h"
#include "monoclock.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "warnp.h"
#include "wire.h"

struct tokyo_state {
	/* State used for spewing requests. */
	struct wire_requestqueue * Q;
	char ** keys;
	size_t N;
	size_t Nip;
	int failed;
	int done;

	/* Temporary key structure. */
	struct kvldskey * key;
};

static int callback_done(void *, int);

static int
sendbatch(struct tokyo_state * C)
{

	while ((C->Nip < 4096) && (C->N < 1000000)) {
		/* Generate the key (which is also used for the value). */
		memcpy(C->key->buf, C->keys[C->N++], 8);

		/* Send the request. */
		if (proto_kvlds_request_set(C->Q, C->key, C->key,
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
	struct tokyo_state * C = cookie;

	/* This request is no longer in progress. */
	C->Nip -= 1;

	/* Did we fail? */
	if (failed) {
		C->done = 1;
		C->failed = 1;
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
tokyo(struct wire_requestqueue * Q, char ** keys)
{
	struct tokyo_state C;
	uint8_t buf[8];	/* dummy */
	struct timeval tv_start, tv_end;

	/* Initialize. */
	C.Q = Q;
	C.keys = keys;
	C.N = 0;
	C.Nip = 0;
	C.failed = 0;
	C.done = 0;

	/* Allocate key structure. */
	memset(buf, 0, 8);
	if ((C.key = kvldskey_create(buf, 8)) == NULL)
		return (-1);

	/* Get start time. */
	if (monoclock_get(&tv_start)) {
		warnp("Error reading clock");
		return (-1);
	}

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		return (-1);

	/* Wait until we've finished. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		return (-1);
	}

	/* Get start time. */
	if (monoclock_get(&tv_end)) {
		warnp("Error reading clock");
		return (-1);
	}

	/* Free the key structure. */
	kvldskey_free(C.key);

	/* Print time. */
	printf("%.3f\n", (tv_end.tv_sec - tv_start.tv_sec) +
	    (tv_end.tv_usec - tv_start.tv_usec) * 0.000001);

	/* Success! */
	return (0);
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;
	char ** keys;
	size_t i;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: tokyo %s\n", "<socketname>");
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

	/* Create keys. */
	if ((keys = malloc(1000000 * sizeof(char *))) == NULL) {
		warnp("malloc");
		exit(1);
	}
	for (i = 0; i < 1000000; i++) {
		if (asprintf(&keys[i], "%08zu", i) == -1) {
			warnp("asprintf");
			exit(1);
		}
	}

	/* Run benchmark. */
	if (tokyo(Q, keys))
		exit(1);

	/* Free keys. */
	for (i = 0; i < 1000000; i++)
		free(keys[i]);
	free(keys);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Success! */
	exit(0);
}

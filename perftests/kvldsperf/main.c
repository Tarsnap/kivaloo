#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

struct createmany_state {
	struct wire_requestqueue * Q;
	size_t Nsent;
	size_t Nmax;
	size_t Nip;
	int failed;
	int done;

	/* Temporary key. */
	struct kvldskey * key;
};

static int callback_done(void *, int);

static int
sendbatch(struct createmany_state * C)
{

	while ((C->Nsent < C->Nmax) && (C->Nip < 4096)) {
		/* Construct the key/value. */
		be64enc(C->key->buf, C->Nsent);

		/* Send the request. */
		if (proto_kvlds_request_set(C->Q, C->key, C->key,
		    callback_done, C))
			goto err0;
		C->Nsent += 1;
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
	struct createmany_state * C = cookie;

	/* This request is no longer in progress. */
	C->Nip -= 1;

	/* Did we fail? */
	if (failed)
		C->failed = 1;

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
createmany(struct wire_requestqueue * Q, size_t N)
{
	struct createmany_state C;
	uint8_t buf[8];	/* dummy */

	/* Initialize. */
	C.Q = Q;
	C.Nsent = 0;
	C.Nmax = N;
	C.Nip = 0;
	C.failed = 0;
	C.done = 0;

	/* Allocate key structure. */
	memset(buf, 0, 8);
	if ((C.key = kvldskey_create(buf, 8)) == NULL)
		return (-1);

	/* Send an initial batch of 4096 requests. */
	if (sendbatch(&C))
		return (-1);

	/* Wait for N SETs to complete. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		return (-1);
	}

	/* Free the key structure. */
	kvldskey_free(C.key);

	/* Success! */
	return (0);
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
		fprintf(stderr, "usage: test_kvlds %s\n", "<socketname>");
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

	/* Test creating 10000000 key-value pairs. */
	if (createmany(Q, 10000000))
		exit(1);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Success! */
	exit(0);
}

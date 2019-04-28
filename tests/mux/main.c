#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "sysendian.h"
#include "wire.h"
#include "warnp.h"

static int op_done = 0;
static int op_failed = 0;
static int op_p = 0;
static int op_badval = 0;
static size_t op_count = 0;

static int
callback_done(void * cookie, int failed)
{

	(void)cookie; /* UNUSED */

	/* Did we fail? */
	if (failed) {
		op_failed = 1;
		op_done = 1;
	}

	/* Decrement the counter. */
	op_count -= 1;

	/* Are we done? */
	if (op_count == 0)
		op_done = 1;

	/* Success! */
	return (0);
}

static int
callback_donep(void * cookie, int failed, int done)
{

	(void)cookie; /* UNUSED */

	/* We're done! */
	op_failed = failed;
	op_p = done;
	op_done = 1;

	/* Success! */
	return (0);
}

static int
callback_get(void * cookie, int failed, struct kvldskey * value)
{
	struct kvldskey * value_correct = cookie;

	/* Record failure status. */
	if (failed) {
		op_failed = 1;
		op_done = 1;
	}

	/* Check that the value matches. */
	if (failed == 0) {
		if ((value == NULL) ^ (value_correct == NULL))
			goto bad;
		if ((value != NULL) &&
		    (value->len != value_correct->len))
			goto bad;
		if ((value != NULL) &&
		    memcmp(value->buf, value_correct->buf, value->len))
			goto bad;
		kvldskey_free(value);
	}

	/* Decrement the counter. */
	op_count -= 1;

	/* Are we done? */
	if (op_count == 0)
		op_done = 1;

	/* Success! */
	return (0);

bad:
	op_done = 1;
	op_badval = 1;
	return (0);
}

static int
callback_range(void * cookie,
    const struct kvldskey * key, const struct kvldskey * value)
{
	struct wire_requestqueue * Q = cookie;

	(void)value; /* UNUSED */

	/* Delete the key-value pair. */
	op_count += 1;
	if (proto_kvlds_request_delete(Q, key, callback_done, NULL)) {
		warnp("Error sending DELETE request");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
set(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_set(Q, key, value, callback_done, NULL)) {
		warnp("Error sending SET request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("SET request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
cas(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * oval,
    const struct kvldskey * value)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_cas(Q, key, oval, value, callback_donep, NULL)) {
		warnp("Error sending CAS request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("CAS request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
cad(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * oval)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_cad(Q, key, oval, callback_donep, NULL)) {
		warnp("Error sending CAD request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("CAD request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
pingpong(struct wire_requestqueue * Q, const char * key, const char * to,
    const char * from, int start)
{
	struct kvldskey * k;
	struct kvldskey * v0, * v1;
	int i;

	/* Create key and values. */
	if ((k = kvldskey_create((const uint8_t *)key, strlen(key))) == NULL)
		return (-1);
	if ((v0 = kvldskey_create((const uint8_t *)from, strlen(from))) == NULL)
		return (-1);
	if ((v1 = kvldskey_create((const uint8_t *)to, strlen(to))) == NULL)
		return (-1);

	/* Write initial value if we're starting the pingpong. */
	if (start) {
		if (set(Q, k, v1))
			return (-1);
	}

	/* Pingpong 100 times. */
	for (i = start; i < 100; ) {
		if (cas(Q, k, v0, v1))
			return (-1);
		if (op_p)
			i++;
	}

	/* Delete final value if necessary. */
	if (start) {
		do {
			if (cad(Q, k, v0))
				return (-1);
			if (op_p)
				break;
		} while (1);
	}

	/* Delete the values and key. */
	kvldskey_free(v1);
	kvldskey_free(v0);
	kvldskey_free(k);

	/* Success! */
	return (0);
}

static int
createmany(struct wire_requestqueue * Q, size_t N, char * prefix)
{
	size_t i;
	struct kvldskey * key;
	struct kvldskey * key2;
	struct kvldskey ** values;
	size_t plen = strlen(prefix);
	uint8_t * keybuf = malloc(plen + 8);
	char valbuf[20];

	/* Copy prefix in. */
	memcpy(keybuf, prefix, plen);

	/* Allocate values structures. */
	values = malloc(N * sizeof(struct kvldskey *));
	for (i = 0; i < N; i++) {
		sprintf(valbuf, "%zu", i);
		values[i] = kvldskey_create((uint8_t *)valbuf, strlen(valbuf));
	}

	/* Store N key-value pairs. */
	op_done = 0;
	op_count = N;
	for (i = 0; i < N; i++) {
		be64enc(&keybuf[plen], i);
		key = kvldskey_create(keybuf, plen + 8);
		if (proto_kvlds_request_set(Q, key, values[i],
		    callback_done, NULL))
			return (-1);
		kvldskey_free(key);
	}

	/* Wait for SETs to complete. */
	if (events_spin(&op_done) || op_failed) {
		warnp("SET request failed");
		return (-1);
	}

	/* Read the values back and check that they are correct. */
	op_done = 0;
	op_failed = 0;
	op_count = N;
	for (i = 0; i < N; i++) {
		be64enc(&keybuf[plen], i);
		key = kvldskey_create(keybuf, plen + 8);
		if (proto_kvlds_request_get(Q, key, callback_get,
		    (void *)(uintptr_t)values[i])) {
			warnp("Error sending GET request");
			return (-1);
		}
		kvldskey_free(key);
	}
	if (events_spin(&op_done) || op_failed) {
		warnp("GET request failed");
		return (-1);
	}
	if (op_badval) {
		warnp("Bad value returned by GET!");
		return (-1);
	}

	/* Free values. */
	for (i = 0; i < N; i++)
		kvldskey_free(values[i]);
	free(values);

	/* Delete all the values. */
	be64enc(&keybuf[plen], 0);
	key = kvldskey_create(keybuf, plen + 8);
	be64enc(&keybuf[plen], N);
	key2 = kvldskey_create(keybuf, plen + 8);
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_range2(Q, key, key2, callback_range,
	    callback_done, Q))
		return (-1);
	kvldskey_free(key2);
	kvldskey_free(key);

	/* Wait for RANGEs and DELETEs to complete. */
	if (events_spin(&op_done) || op_failed) {
		warnp("RANGE or DELETE request failed");
		return (-1);
	}

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
	if (argc != 3) {
		fprintf(stderr, "usage: test_mux %s %s\n",
		    "<socketname>", "{ping | pong | <prefix>}");
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

	if (strcmp(argv[2], "ping") == 0) {
		if (pingpong(Q, "pingpong", "ping", "pong", 1))
			exit(1);
	} else if (strcmp(argv[2], "pong") == 0) {
		if (pingpong(Q, "pingpong", "pong", "ping", 0))
			exit(1);
	} else if (strcmp(argv[2], "loop") == 0) {
		/* Repeatedly create/read/delete 10^4 pairs until we die. */
		do {
			if (createmany(Q, 10000, argv[2]))
				exit(1);
		} while (1);
	} else {
		/* Test creating 10000 pairs and reading them back. */
		if (createmany(Q, 10000, argv[2]))
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

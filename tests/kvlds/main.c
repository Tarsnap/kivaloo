#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kivaloo.h"
#include "kvldskey.h"
#include "parsenum.h"
#include "proto_kvlds.h"
#include "sysendian.h"
#include "warnp.h"

static int op_done = 0;
static int op_failed = 0;
static int op_p = 0;
static int op_badval = 0;
static size_t op_count = 0;

static int
callback_params(void * cookie, int failed, size_t kmax, size_t vmax)
{

	(void)cookie; /* UNUSED */
	(void)kmax; /* UNUSED */
	(void)vmax; /* UNUSED */

	/* Did we fail? */
	if (failed)
		op_failed = 1;

	/* We're done! */
	op_done = 1;

	/* Success! */
	return (0);
}

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
callback_donep(void * cookie, int failed, int status)
{

	(void)cookie; /* UNUSED */

	/* We're done! */
	op_failed = failed;
	op_p = status ? 0 : 1;
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
add(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value, int noop)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_add(Q, key, value, callback_donep, NULL)) {
		warnp("Error sending ADD request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("ADD request failed");
		goto err0;
	}

	/* Check for no-op. */
	if (noop && op_p) {
		warn0("ADD should have been a no-op");
		goto err0;
	}
	if (!noop && !op_p) {
		warn0("ADD should not have been a no-op");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
modify(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value, int noop)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_modify(Q, key, value, callback_donep, NULL)) {
		warnp("Error sending MODIFY request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("MODIFY request failed");
		goto err0;
	}

	/* Check for no-op. */
	if (noop && op_p) {
		warn0("MODIFY should have been a no-op");
		goto err0;
	}
	if (!noop && !op_p) {
		warn0("MODIFY should not have been a no-op");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
delete(struct wire_requestqueue * Q, const struct kvldskey * key)
{

	/* Send the request. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_delete(Q, key, callback_done, NULL)) {
		warnp("Error sending DELETE request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("DELETE request failed");
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
    const struct kvldskey * value, int noop)
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

	/* Check for no-op. */
	if (noop && op_p) {
		warn0("CAS should have been a no-op");
		goto err0;
	}
	if (!noop && !op_p) {
		warn0("CAS should not have been a no-op");
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
    const struct kvldskey * key, const struct kvldskey * oval, int noop)
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

	/* Check for no-op. */
	if (noop && op_p) {
		warn0("CAD should have been a no-op");
		goto err0;
	}
	if (!noop && !op_p) {
		warn0("CAD should not have been a no-op");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
verify(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value)
{

	/* Perform a get. */
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_get(Q, key, callback_get,
	    (void *)(uintptr_t)value)) {
		warnp("Error sending GET request");
		goto err0;
	}
	if (events_spin(&op_done) || op_failed) {
		warnp("GET request failed");
		goto err0;
	}
	if (op_badval) {
		warn0("Bad value returned by GET!");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
doparams(struct wire_requestqueue * Q)
{

	/* Send the request. */
	op_done = 0;
	if (proto_kvlds_request_params(Q, callback_params, NULL)) {
		warnp("Error sending PARAMS request");
		goto err0;
	}

	/* Wait for it to finish. */
	if (events_spin(&op_done) || op_failed) {
		warnp("PARAMS request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
mutate(struct wire_requestqueue * Q)
{
	struct kvldskey * key = kvldskey_create((const uint8_t *)"key", 3);
	struct kvldskey * value = kvldskey_create((const uint8_t *)"value", 5);
	struct kvldskey * value2;
	uint8_t buf[100];
	uint8_t i;

	/* Initialize value2. */
	for (i = 0; i < 100; i++)
		buf[i] = i;
	value2 = kvldskey_create(buf, 100);

	/*
	 * Test B+Tree mutation code paths, one by one:
	 * 1. SET on a key which is not present.
	 * 2. SET on a key which is present.
	 * 3. ADD on a key which is present.
	 * 4. DELETE of a key which is present.
	 * 5. DELETE of a key which is not present.
	 * 6. MODIFY of a key which is not present.
	 * 7. ADD of a key which is not present.
	 * 8. MODIFY of a key which is present.
	 * 9. CAS of a key with non-matching value.
	 * 10. CAS of a key with matching value.
	 * 11. CAD of a key with non-matching value.
	 * 12. CAD of a key with matching value.
	 * 13. CAS of a key which is not present.
	 * 14. CAD of a key which is not present.
	 */

	/* Set key = value. */
	if (set(Q, key, value) || verify(Q, key, value))
		goto err1;

	/* Set key = value2. */
	if (set(Q, key, value2) || verify(Q, key, value2))
		goto err1;

	/* Add key = value (should be a no-op). */
	if (add(Q, key, value, 1) || verify(Q, key, value2))
		goto err1;

	/* Delete key. */
	if (delete(Q, key) || verify(Q, key, NULL))
		goto err1;

	/* Delete key (even though it isn't there). */
	if (delete(Q, key) || verify(Q, key, NULL))
		goto err1;

	/* Modify key = value (should be a no-op). */
	if (modify(Q, key, value, 1) || verify(Q, key, NULL))
		goto err1;

	/* Add key = value. */
	if (add(Q, key, value, 0) || verify(Q, key, value))
		goto err1;

	/* Modify key = value2. */
	if (modify(Q, key, value, 0) || verify(Q, key, value))
		goto err1;

	/* CAS key = value2 -> value2 (should be a no-op). */
	if (cas(Q, key, value2, value2, 1) || verify(Q, key, value))
		goto err1;

	/* CAS key = value -> value2. */
	if (cas(Q, key, value, value2, 0) || verify(Q, key, value2))
		goto err1;

	/* CAD key = value -> NULL (should be a no-op). */
	if (cad(Q, key, value, 1) || verify(Q, key, value2))
		goto err1;

	/* CAD key = value2 -> NULL. */
	if (cad(Q, key, value2, 0) || verify(Q, key, NULL))
		goto err1;

	/* CAS key = value -> value2 (should be a no-op). */
	if (cas(Q, key, value, value2, 1) || verify(Q, key, NULL))
		goto err1;

	/* CAD key = value -> NULL (should be a no-op). */
	if (cad(Q, key, value, 1) || verify(Q, key, NULL))
		goto err1;

	/* Clean up. */
	kvldskey_free(value2);
	kvldskey_free(value);
	kvldskey_free(key);

	/* Success! */
	return (0);

err1:
	kvldskey_free(value2);
	kvldskey_free(value);
	kvldskey_free(key);

	/* Failure! */
	return (-1);
}

static int
createmany(struct wire_requestqueue * Q, size_t N)
{
	size_t i;
	struct kvldskey * key;
	struct kvldskey * key2;
	struct kvldskey ** values;
	uint8_t keybuf[8];
	char valbuf[20];

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
		be64enc(keybuf, i);
		key = kvldskey_create(keybuf, 8);
		if (proto_kvlds_request_set(Q, key, values[i],
		    callback_done, NULL))
			goto err1;
		kvldskey_free(key);
	}

	/* Wait for SETs to complete. */
	if (events_spin(&op_done) || op_failed) {
		warnp("SET request failed");
		goto err1;
	}

	/* Read the values back and check that they are correct. */
	op_done = 0;
	op_failed = 0;
	op_count = N;
	for (i = 0; i < N; i++) {
		be64enc(keybuf, i);
		key = kvldskey_create(keybuf, 8);
		if (proto_kvlds_request_get(Q, key, callback_get,
		    (void *)(uintptr_t)values[i])) {
			warnp("Error sending GET request");
			goto err1;
		}
		kvldskey_free(key);
	}
	if (events_spin(&op_done) || op_failed) {
		warnp("GET request failed");
		goto err1;
	}
	if (op_badval) {
		warn0("Bad value returned by GET!");
		goto err1;
	}

	/* Free values. */
	for (i = 0; i < N; i++)
		kvldskey_free(values[i]);
	free(values);

	/* Delete all the values. */
	be64enc(keybuf, 0);
	key = kvldskey_create(keybuf, 8);
	be64enc(keybuf, N);
	key2 = kvldskey_create(keybuf, 8);
	op_done = 0;
	op_count = 1;
	if (proto_kvlds_request_range2(Q, key, key2, callback_range,
	    callback_done, Q))
		goto err0;
	kvldskey_free(key2);
	kvldskey_free(key);

	/* Wait for RANGEs and DELETEs to complete. */
	if (events_spin(&op_done) || op_failed) {
		warnp("RANGE or DELETE request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	for (i = 0; i < N; i++)
		kvldskey_free(values[i]);
	free(values);
err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct wire_requestqueue * Q;
	struct kivaloo_cookie * K;
	size_t num_pairs = 40000;

	WARNP_INIT;

	/* Check number of arguments. */
	if ((argc != 2) && (argc != 3)) {
		fprintf(stderr, "usage: test_kvlds %s %s\n", "<socketname>",
		    "[num_pairs]");
		exit(1);
	}

	/* Override the default number of pairs to test. */
	if (argc == 3) {
		if (PARSENUM(&num_pairs, argv[2])) {
			warnp("parsnum");
			exit(1);
		}
	}

	/* Open a connection to KVLDS. */
	if ((K = kivaloo_open(argv[1], &Q)) == NULL) {
		warnp("Could not connect to KVLDS daemon.");
		goto err0;
	}

	/* Get maximum key/value lengths. */
	if (doparams(Q))
		goto err1;

	/* Test B+Tree mutation code paths. */
	if (mutate(Q))
		goto err1;

	/* Test creating key-value pairs and reading them back. */
	if (createmany(Q, num_pairs))
		goto err1;

	/* Free the request queue and network connection. */
	kivaloo_close(K);

	/* Success! */
	exit(0);

err1:
	kivaloo_close(K);
err0:
	/* Failure! */
	exit(1);
}

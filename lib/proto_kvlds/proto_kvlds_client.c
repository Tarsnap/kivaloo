#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "imalloc.h"
#include "mpool.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "proto_kvlds.h"

static int callback_params(void *, uint8_t *, size_t);
static int callback_done(void *, uint8_t *, size_t);
static int callback_donep(void *, uint8_t *, size_t);
static int callback_get(void *, uint8_t *, size_t);
static int callback_range(void *, uint8_t *, size_t);
static int callback_range2(void *, int, size_t, struct kvldskey *,
    struct kvldskey **, struct kvldskey **);
static int poke_range2(void *);

struct params_cookie {
	int (* callback)(void *, int, size_t, size_t);
	void * cookie;
};

struct done_cookie {
	int (* callback)(void *, int);
	void * cookie;
	const char * type;
};

struct donep_cookie {
	int (* callback)(void *, int, int);
	void * cookie;
	const char * type;
};

struct get_cookie {
	int (* callback)(void *, int, struct kvldskey *);
	void * cookie;
};

struct range_cookie {
	int (* callback)(void *, int, size_t, struct kvldskey *,
	    struct kvldskey **, struct kvldskey **);
	void * cookie;
	size_t max;
};

struct range2_cookie {
	struct wire_requestqueue * Q;
	int (* callback_item)(void *,
	    const struct kvldskey *, const struct kvldskey *);
	int (* callback)(void *, int);
	void * cookie;
	int failed;
	int reqdone;
	struct kvldskey * start;
	struct kvldskey * end;
};

MPOOL(done, struct done_cookie, 4096);
MPOOL(donep, struct donep_cookie, 4096);
MPOOL(get, struct get_cookie, 4096);

/* Macro for simplifying response-parsing errors. */
#define BAD(rtype, ftype)	do {				\
	warn0("Received %s response with %s", rtype, ftype);	\
	goto failed;						\
} while (0)

/* Process a PARAMS response. */
static int
callback_params(void * cookie, uint8_t * buf, size_t buflen)
{
	struct params_cookie * C = cookie;
	int failed = 1;
	size_t kmax = 0;
	size_t vmax = 0;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the packet length sane? */
		if (buflen != 8)
			BAD("params", "bogus length");

		/* Parse values. */
		kmax = be32dec(&buf[0]);
		vmax = be32dec(&buf[4]);

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, kmax, vmax);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/* Process a success response. */
static int
callback_done(void * cookie, uint8_t * buf, size_t buflen)
{
	struct done_cookie * C = cookie;
	int failed = 1;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD(C->type, "bogus length");
		if (be32dec(&buf[0]) > 0)
			BAD(C->type, "bogus status code");

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed);

	/* Free the cookie. */
	mpool_done_free(C);

	/* Return status from callback. */
	return (rc);
}

/* Process a done/non-done response. */
static int
callback_donep(void * cookie, uint8_t * buf, size_t buflen)
{
	struct donep_cookie * C = cookie;
	int failed = 1;
	int status = 0;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD(C->type, "bogus length");
		if (be32dec(&buf[0]) > 1)
			BAD(C->type, "bogus status code");
		status = be32dec(&buf[0]);

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, status);

	/* Free the cookie. */
	mpool_donep_free(C);

	/* Return status from callback. */
	return (rc);
}

/* Process a GET response. */
static int
callback_get(void * cookie, uint8_t * buf, size_t buflen)
{
	struct get_cookie * C = cookie;
	int failed = 1;
	int status = 0;
	struct kvldskey * value = NULL;
	size_t valuelen = 0;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD("GET", "bogus length");
		if (be32dec(&buf[0]) > 1)
			BAD("GET", "bogus status code");
		status = be32dec(&buf[0]);

		/* Parse a value if one exists. */
		if (status == 0) {
			if ((valuelen = kvldskey_unserialize(&value, &buf[4],
			    buflen - 4)) == 0) {
				warnp("Error parsing GET response value");
				goto failed;
			}
		} else {
			value = NULL;
			valuelen = 0;
		}

		/* Verify packet length. */
		if (buflen != 4 + valuelen)
			BAD("GET", "wrong length");

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, value);

	/* Free the cookie. */
	mpool_get_free(C);

	/* Return status from callback. */
	return (rc);
}

/* Process a RANGE response. */
static int
callback_range(void * cookie, uint8_t * buf, size_t buflen)
{
	struct range_cookie * C = cookie;
	int failed = 1;
	size_t bufpos = 0;
	size_t nkeys = 0;
	struct kvldskey * next = NULL;
	struct kvldskey ** keys = NULL;
	struct kvldskey ** values = NULL;
	size_t klen;
	size_t i;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen - bufpos < 4)
			BAD("RANGE", "bogus length");
		if (be32dec(&buf[bufpos]) != 0)
			BAD("RANGE", "bogus status code");
		bufpos += 4;

		/* Parse and sanity-check the number of key-value pairs. */
		if (buflen - bufpos < 4)
			BAD("RANGE", "bogus length");
		nkeys = be32dec(&buf[bufpos]);
		if ((nkeys > 1) && (nkeys > C->max / 2))
			BAD("RANGE", "too many key-value pairs");
		bufpos += 4;

		/* Parse next key. */
		if ((klen = kvldskey_unserialize(&next, &buf[bufpos],
		    buflen - bufpos)) == 0) {
			warnp("Error parsing RANGE response next key");
			goto failed;
		}
		bufpos += klen;

		/* Allocate buffer for keys. */
		if (IMALLOC(keys, nkeys, struct kvldskey *))
			goto failed;
		for (i = 0; i < nkeys; i++)
			keys[i] = NULL;

		/* Allocate buffer for values. */
		if (IMALLOC(values, nkeys, struct kvldskey *))
			goto failed;
		for (i = 0; i < nkeys; i++)
			values[i] = NULL;

		/* Parse keys and values. */
		for (i = 0; i < nkeys; i++) {
			if ((klen = kvldskey_unserialize(&keys[i],
			    &buf[bufpos], buflen - bufpos)) == 0) {
				warnp("Error parsing RANGE response key");
				goto failed;
			}
			bufpos += klen;

			if ((klen = kvldskey_unserialize(&values[i],
			    &buf[bufpos], buflen - bufpos)) == 0) {
				warnp("Error parsing RANGE response value");
				goto failed;
			}
			bufpos += klen;
		}

		/* Make sure we reached the end of the packet. */
		if (buflen != bufpos)
			BAD("RANGE", "wrong length");

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* If we failed, clean up. */
	if (failed) {
		if (values) {
			for (i = 0; i < nkeys; i++)
				kvldskey_free(values[i]);
			free(values);
			values = NULL;
		}
		if (keys) {
			for (i = 0; i < nkeys; i++)
				kvldskey_free(keys[i]);
			free(keys);
			keys = NULL;
		}
		nkeys = 0;
		kvldskey_free(next);
		next = NULL;
	}

	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, nkeys, next, keys, values);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);

}

/* Do callbacks for a RANGE response. */
static int
callback_range2(void * cookie, int failed, size_t nkeys,
    struct kvldskey * next,
    struct kvldskey ** keys, struct kvldskey ** values)
{
	struct range2_cookie * C = cookie;
	size_t i;
	int rc = 0;

	/* We've done a request. */
	C->reqdone = 1;

	/* If we failed, record it and skip to poking. */
	if (failed) {
		C->failed = 1;
		goto poke;
	}

	/* Record our new starting position. */
	kvldskey_free(C->start);
	C->start = next;

	/* Invoke callbacks. */
	for (i = 0; i < nkeys; i++) {
		if ((C->callback_item)(C->cookie, keys[i], values[i]))
			rc = -1;
	}

	/* Free keys. */
	for (i = 0; i < nkeys; i++)
		kvldskey_free(keys[i]);
	free(keys);

	/* Free values. */
	for (i = 0; i < nkeys; i++)
		kvldskey_free(values[i]);
	free(values);

poke:
	/* Do another RANGE request or perform the final callback. */
	if (poke_range2(cookie))
		rc = -1;

	/* Return success or failure if anything failed. */
	return (rc);
}

/**
 * proto_kvlds_request_params(Q, callback, cookie):
 * Send a PARAMS request to get the maximum key and value lengths via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, kmax, vmax)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * kmax is the maximum key length, and vmax is the maximum value length.
 */
int
proto_kvlds_request_params(struct wire_requestqueue * Q,
    int (* callback)(void *, int, size_t, size_t), void * cookie)
{
	struct params_cookie * C;
	uint8_t * buf;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct params_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, 4,
	    callback_params, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_PARAMS);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, 4))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_set(Q, key, value, callback, cookie):
 * Send a SET request to associate the value ${value} with the key ${key} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int
proto_kvlds_request_set(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value,
    int (* callback)(void *, int), void * cookie)
{
	struct done_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* Bake a cookie. */
	if ((C = mpool_done_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "SET";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);
	buflen += kvldskey_serial_size(value);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_done, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_SET);
	bufpos = 4;
	kvldskey_serialize(key, &buf[bufpos]);
	bufpos += kvldskey_serial_size(key);
	kvldskey_serialize(value, &buf[bufpos]);
	bufpos += kvldskey_serial_size(value);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_done_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_cas(Q, key, oval, value, callback, cookie):
 * Send a CAS request to associate the value ${value} with the key ${key} iff
 * the current value is ${oval} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int
proto_kvlds_request_cas(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * oval,
    const struct kvldskey * value,
    int (* callback)(void *, int, int), void * cookie)
{
	struct donep_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* Bake a cookie. */
	if ((C = mpool_donep_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "CAS";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);
	buflen += kvldskey_serial_size(oval);
	buflen += kvldskey_serial_size(value);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_donep, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_CAS);
	bufpos = 4;
	kvldskey_serialize(key, &buf[bufpos]);
	bufpos += kvldskey_serial_size(key);
	kvldskey_serialize(oval, &buf[bufpos]);
	bufpos += kvldskey_serial_size(oval);
	kvldskey_serialize(value, &buf[bufpos]);
	bufpos += kvldskey_serial_size(value);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_donep_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_add(Q, key, value, callback, cookie):
 * Send an ADD request to associate the value ${value} with the key ${key}
 * iff there is no current value set via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int
proto_kvlds_request_add(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value,
    int (* callback)(void *, int, int), void * cookie)
{
	struct donep_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* Bake a cookie. */
	if ((C = mpool_donep_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "ADD";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);
	buflen += kvldskey_serial_size(value);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_donep, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_ADD);
	bufpos = 4;
	kvldskey_serialize(key, &buf[bufpos]);
	bufpos += kvldskey_serial_size(key);
	kvldskey_serialize(value, &buf[bufpos]);
	bufpos += kvldskey_serial_size(value);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_donep_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_modify(Q, key, value, callback, cookie):
 * Send a MODIFY request to associate the value ${value} with the key ${key}
 * iff there is a current value set via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int
proto_kvlds_request_modify(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * value,
    int (* callback)(void *, int, int), void * cookie)
{
	struct donep_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* Bake a cookie. */
	if ((C = mpool_donep_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "MODIFY";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);
	buflen += kvldskey_serial_size(value);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_donep, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_MODIFY);
	bufpos = 4;
	kvldskey_serialize(key, &buf[bufpos]);
	bufpos += kvldskey_serial_size(key);
	kvldskey_serialize(value, &buf[bufpos]);
	bufpos += kvldskey_serial_size(value);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_donep_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_delete(Q, key, callback, cookie):
 * Send a DELETE request to delete the value (if any) associated with the
 * key ${key} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int
proto_kvlds_request_delete(struct wire_requestqueue * Q,
    const struct kvldskey * key,
    int (* callback)(void *, int), void * cookie)
{
	struct done_cookie * C;
	uint8_t * buf;
	size_t buflen;

	/* Bake a cookie. */
	if ((C = mpool_done_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "DELETE";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_done, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_DELETE);
	kvldskey_serialize(key, &buf[4]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_done_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_cad(Q, key, oval, callback, cookie):
 * Send a CAD request to delete the value associated with the key ${key}
 * iff it is currently ${oval} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was deleted and 1 if it was not deleted.
 */
int
proto_kvlds_request_cad(struct wire_requestqueue * Q,
    const struct kvldskey * key, const struct kvldskey * oval,
    int (* callback)(void *, int, int), void * cookie)
{
	struct donep_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* Bake a cookie. */
	if ((C = mpool_donep_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->type = "CAD";

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);
	buflen += kvldskey_serial_size(oval);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_donep, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_CAD);
	bufpos = 4;
	kvldskey_serialize(key, &buf[bufpos]);
	bufpos += kvldskey_serial_size(key);
	kvldskey_serialize(oval, &buf[bufpos]);
	bufpos += kvldskey_serial_size(oval);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_donep_free(C);
err0:
	/* Failure! */
	return (-1);
}


/**
 * proto_kvlds_request_get(Q, key, callback, cookie):
 * Send a GET request to read the value associated with the key ${key} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, value)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and value is the value associated with the key ${key} or NULL if no value
 * associated.  The callback is responsible for freeing ${value}.
 */
int
proto_kvlds_request_get(struct wire_requestqueue * Q,
    const struct kvldskey * key,
    int (* callback)(void *, int, struct kvldskey *), void * cookie)
{
	struct get_cookie * C;
	uint8_t * buf;
	size_t buflen;

	/* Bake a cookie. */
	if ((C = mpool_get_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request size. */
	buflen = 4;
	buflen += kvldskey_serial_size(key);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_get, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_GET);
	kvldskey_serialize(key, &buf[4]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_get_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_range(Q, start, end, max, callback, cookie):
 * Send a RANGE request to list key-value pairs which are >= ${start} and
 * < ${end} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, nkeys, keys, values)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * nkeys is the number of key-value pairs returned, and keys and values are
 * arrays of keys and values respectively.  The callback is responsible for
 * freeing the arrays and their members.
 */
int
proto_kvlds_request_range(struct wire_requestqueue * Q,
    const struct kvldskey * start, const struct kvldskey * end, size_t max,
    int (* callback)(void *, int, size_t, struct kvldskey *,
	struct kvldskey **, struct kvldskey **),
    void * cookie)
{
	struct range_cookie * C;
	uint8_t * buf;
	size_t buflen;
	size_t bufpos;

	/* The request size must fit into a uint32_t. */
	if (max >= UINT32_MAX)
		max = UINT32_MAX;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct range_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->max = max;

	/* Compute request size. */
	buflen = 8;
	buflen += kvldskey_serial_size(start);
	buflen += kvldskey_serial_size(end);

	/* Start writing a request. */
	if ((buf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback_range, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&buf[0], PROTO_KVLDS_RANGE);
	be32enc(&buf[4], max);
	bufpos = 8;
	kvldskey_serialize(start, &buf[bufpos]);
	bufpos += kvldskey_serial_size(start);
	kvldskey_serialize(end, &buf[bufpos]);
	bufpos += kvldskey_serial_size(end);

	/* Sanity-check. */
	assert(bufpos == buflen);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, buf, buflen))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_range2(Q, start, end, callback_item, callback, cookie):
 * Repeatedly use proto_kvlds_request_range to issue RANGE requests via the
 * request queue ${Q}.  Invoke
 *     ${callback_item}(${cookie}, key, value)
 * for each key-value pair returned, and invoke
 *     ${callback}(${cookie}, failed)
 * when all key-value pairs in the specified range have been handled.
 */
int
proto_kvlds_request_range2(struct wire_requestqueue * Q,
    const struct kvldskey * start, const struct kvldskey * end,
    int (* callback_item)(void *,
	const struct kvldskey *, const struct kvldskey *),
    int (* callback)(void *, int), void * cookie)
{
	struct range2_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct range2_cookie))) == NULL)
		goto err0;
	C->Q = Q;
	C->callback_item = callback_item;
	C->callback = callback;
	C->cookie = cookie;
	C->failed = 0;
	C->reqdone = 0;

	/* Duplicate start and end keys. */
	if ((C->start = kvldskey_create(start->buf, start->len)) == NULL)
		goto err1;
	if ((C->end = kvldskey_create(end->buf, end->len)) == NULL)
		goto err2;

	/* Start issuing RANGE requests. */
	if (!events_immediate_register(poke_range2, C, 0))
		goto err3;

	/* Success! */
	return (0);

err3:
	kvldskey_free(C->end);
err2:
	kvldskey_free(C->start);
err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* Issue a RANGE request or do final callback. */
static int
poke_range2(void * cookie)
{
	struct range2_cookie * C = cookie;
	int doreq;
	int rc = 0;

	/* We need to issue a RANGE request iff start < end... */
	if (kvldskey_cmp(C->start, C->end) < 0)
		doreq = 1;
	else
		doreq = 0;

	/* ... except that an end of "" is special... */
	if (C->end->len == 0) {
		/*
		 * We want to issue a RANGE request unless start is also ""
		 * AND we've already done at least one RANGE request (i.e.,
		 * start is the empty key at the end of the keyspace, not the
		 * empty key at the beginning of the keyspace).
		 */
		if ((C->reqdone != 0) && (C->start->len == 0))
			doreq = 0;
		else
			doreq = 1;
	}

	/* ... but if we've failed, we never want to do any requests. */
	if (C->failed)
		doreq = 0;

	/* Send a RANGE request if we need one. */
	if (doreq) {
		if (proto_kvlds_request_range(C->Q, C->start, C->end,
		    0x100000, callback_range2, C))
			goto err0;
		goto done;
	}

	/* Invoke the completion callback. */
	rc = (C->callback)(C->cookie, C->failed);

	/* Free the cookie. */
	kvldskey_free(C->start);
	kvldskey_free(C->end);
	free(C);

done:
	/* Return success or status code from callback. */
	return (rc);

err0:
	/* Failure! */
	return (-1);
}

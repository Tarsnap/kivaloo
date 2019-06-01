#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "proto_dynamodb_kv.h"

struct status_cookie {
	int (* callback)(void *, int);
	void * cookie;
};

struct data_cookie {
	int (* callback)(void *, int, const uint8_t *, size_t);
	void * cookie;
};

/* Macro for simplifying response-parsing errors. */
#define BAD(rtype, ftype)	do {				\
	warn0("Received %s response with %s", rtype, ftype);	\
	goto failed;						\
} while (0)

static int
callback_status(void * cookie, uint8_t * buf, size_t buflen)
{
	struct status_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen != 4)
			BAD("status", "bogus length");

		/* Parse the status. */
		status = be32dec(&buf[0]);

		/* Did the operation succeed? */
		switch (status) {
		case 0:
			failed = 0;
			break;
		case 1:
			break;
		default:
			BAD("status", "invalid status");
		}
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

static int
callback_data(void * cookie, uint8_t * buf, size_t buflen)
{
	struct data_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	uint32_t len = 0;
	uint8_t * dbuf = NULL;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen < 4)
			BAD("GET", "bogus length");

		/* Parse the status. */
		status = be32dec(&buf[0]);

		/* Non-zero status is a failure. */
		switch (status) {
		case 0:
			break;
		case 1:
		case 2:
			if (buflen != 4)
				BAD("GET", "bogus length");
			failed = (int)status;
			goto failed;
		default:
			BAD("GET", "invalid status");
		}

		/* Parse the data length. */
		if (buflen < 8)
			BAD("GET", "bogus length");
		len = be32dec(&buf[4]);

		/* Do we have the right packet length? */
		if (buflen != 8 + len)
			BAD("GET", "bogus length");

		/* Success!  Point at the data. */
		failed = 0;
		dbuf = &buf[8];
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, dbuf, len);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_dynamodb_kv_request_put(Q, key, buf, buflen, callback, cookie):
 * Send a request to associate the value ${buf} (of length ${buflen}) with
 * the key ${key} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status)
 * upon request completion, where ${status} is 0 on success and 1 on failure.
 * The value must be of length at most 256 kiB.
 */
int
proto_dynamodb_kv_request_put(struct wire_requestqueue * Q, const char * key,
    const uint8_t * buf, size_t buflen,
    int (* callback)(void *, int), void * cookie)
{
	struct status_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate key length. */
	if (strlen(key) > 255) {
		warn0("Key is too long");
		goto err0;
	}

	/* Validate value length. */
	if (buflen > 256 * 1024) {
		warn0("Value is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct status_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 4 + 1 + strlen(key) + 4 + buflen;

	/* Start writing a request. */
	if ((p = rbuf = wire_requestqueue_add_getbuf(Q,
	    rlen, callback_status, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_DDBKV_PUT);
	p += 4;
	*p++ = (uint8_t)strlen(key);
	memcpy(p, key, strlen(key));
	p += strlen(key);
	be32enc(p, (uint32_t)buflen);
	p += 4;
	memcpy(p, buf, buflen);
	p += buflen;

	/* Sanity check. */
	assert(p == &rbuf[rlen]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, rbuf, rlen))
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
 * proto_dynamodb_kv_request_get(Q, key, callback, cookie):
 * Send a request to read the value associated with the key ${key} via the
 * request queue ${Q}.  The value must be of length at most ${maxlen}.
 * Invoke
 *     ${callback}(${cookie}, status, buf, len)
 * upon request completion, where ${status} is 0 on success, 1 on failure,
 * and 2 if there is no such key/value pair; and (on success) ${len} is the
 * length of the value returned via ${buf}.
 */
int
proto_dynamodb_kv_request_get(struct wire_requestqueue * Q, const char * key,
    int (* callback)(void *, int, const uint8_t *, size_t), void * cookie)
{
	struct data_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate key length. */
	if (strlen(key) > 255) {
		warn0("Key is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct data_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 4 + 1 + strlen(key);

	/* Start writing a request. */
	if ((p = rbuf = wire_requestqueue_add_getbuf(Q,
	    rlen, callback_data, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_DDBKV_GET);
	p += 4;
	*p++ = (uint8_t)strlen(key);
	memcpy(p, key, strlen(key));
	p += strlen(key);

	/* Sanity check. */
	assert(p == &rbuf[rlen]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, rbuf, rlen))
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
 * proto_dynamodb_kv_request_getc(Q, key, callback, cookie):
 * As proto_dynamodb_kv_request_get, except that the underlying DynamoDB
 * request is made with strong consistency.
 */
int
proto_dynamodb_kv_request_getc(struct wire_requestqueue * Q, const char * key,
    int (* callback)(void *, int, const uint8_t *, size_t), void * cookie)
{
	struct data_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate key length. */
	if (strlen(key) > 255) {
		warn0("Key is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct data_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 4 + 1 + strlen(key);

	/* Start writing a request. */
	if ((p = rbuf = wire_requestqueue_add_getbuf(Q,
	    rlen, callback_data, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_DDBKV_GETC);
	p += 4;
	*p++ = (uint8_t)strlen(key);
	memcpy(p, key, strlen(key));
	p += strlen(key);

	/* Sanity check. */
	assert(p == &rbuf[rlen]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, rbuf, rlen))
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
 * proto_dynamodb_kv_request_delete(Q, key, callback, cookie):
 * Send a request to delete the key ${key} and its associated value via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status)
 * upon request completion, where ${status} is 0 on success and 1 on failure.
 */
int
proto_dynamodb_kv_request_delete(struct wire_requestqueue * Q,
    const char * key, int (* callback)(void *, int), void * cookie)
{
	struct status_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate key length. */
	if (strlen(key) > 255) {
		warn0("Key is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct status_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 4 + 1 + strlen(key);

	/* Start writing a request. */
	if ((p = rbuf = wire_requestqueue_add_getbuf(Q,
	    rlen, callback_status, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_DDBKV_DELETE);
	p += 4;
	*p++ = (uint8_t)strlen(key);
	memcpy(p, key, strlen(key));
	p += strlen(key);

	/* Sanity check. */
	assert(p == &rbuf[rlen]);

	/* Finish writing request. */
	if (wire_requestqueue_add_done(Q, rbuf, rlen))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "proto_s3.h"

static int callback_put(void *, uint8_t *, size_t);
static int callback_get(void *, uint8_t *, size_t);
static int callback_range(void *, uint8_t *, size_t);
static int callback_head(void *, uint8_t *, size_t);
static int callback_delete(void *, uint8_t *, size_t);

struct put_cookie {
	int (* callback)(void *, int);
	void * cookie;
};

struct get_cookie {
	int (* callback)(void *, int, size_t, const uint8_t *);
	void * cookie;
};

struct head_cookie {
	int (* callback)(void *, int, size_t);
	void * cookie;
};

struct delete_cookie {
	int (* callback)(void *, int);
	void * cookie;
};

/* Macro for simplifying response-parsing errors. */
#define BAD(rtype, ftype)	do {				\
	warn0("Received %s response with %s", rtype, ftype);	\
	goto failed;						\
} while (0)

/**
 * proto_s3_request_put(Q, bucket, object, buflen, buf, callback, cookie):
 * Send a PUT request to store ${buflen} bytes from ${buf} to the object
 * ${object} in the S3 bucket ${bucket} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where ${failed} is 0 on success and 1 on failure.
 */
int
proto_s3_request_put(struct wire_requestqueue * Q, const char * bucket,
    const char * object, size_t buflen, const uint8_t * buf,
    int (* callback)(void *, int), void * cookie)
{
	struct put_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate bucket and object name lengths. */
	if ((strlen(bucket) > 255) || (strlen(object) > 255)) {
		warn0("Bucket or object name is too long");
		goto err0;
	}

	/* Validate buflen. */
	if (buflen >= PROTO_S3_MAXLEN) {
		warn0("PUT length is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct put_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 6 + strlen(bucket) + strlen(object) + 4 + buflen;

	/* Start writing a request. */
	if ((p = rbuf =
	    wire_requestqueue_add_getbuf(Q, rlen, callback_put, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_S3_PUT);
	p += 4;
	*p++ = (uint8_t)strlen(bucket);
	memcpy(p, bucket, strlen(bucket));
	p += strlen(bucket);
	*p++ = (uint8_t)strlen(object);
	memcpy(p, object, strlen(object));
	p += strlen(object);
	be32enc(p, buflen);
	p += 4;
	memcpy(p, buf, buflen);

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

/* PUT response-handling callback. */
static int
callback_put(void * cookie, uint8_t * buf, size_t buflen)
{
	struct put_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen != 4)
			BAD("PUT", "bogus length");

		/* Parse the packet. */
		status = be32dec(&buf[0]);

		/* Did the operation succeed? */
		if (status == 200)
			failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_s3_request_get(Q, bucket, object, maxlen, callback, cookie):
 * Send a GET request to read up to ${maxlen} bytes from the object ${object}
 * in the S3 bucket ${bucket} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, len, buf)
 * upon request completion, where ${failed} is 0 on success and 1 on failure,
 * ${len} is the length of the object (up to ${maxlen}) or -1 (on failure or
 * if the object is larger than ${maxlen} bytes), and ${buf} contains the
 * object data (if ${len} != -1) or is NULL (if ${len} == -1).
 */
int
proto_s3_request_get(struct wire_requestqueue * Q, const char * bucket,
    const char * object, size_t maxlen,
    int (* callback)(void *, int, size_t, const uint8_t *), void * cookie)
{
	struct get_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate bucket and object name lengths. */
	if ((strlen(bucket) > 255) || (strlen(object) > 255)) {
		warn0("Bucket or object name is too long");
		goto err0;
	}

	/* Validate maxlen. */
	if (maxlen > PROTO_S3_MAXLEN) {
		warn0("Maximum GET length is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct get_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 6 + strlen(bucket) + strlen(object) + 4;

	/* Start writing a request. */
	if ((p = rbuf =
	    wire_requestqueue_add_getbuf(Q, rlen, callback_get, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_S3_GET);
	p += 4;
	*p++ = (uint8_t)strlen(bucket);
	memcpy(p, bucket, strlen(bucket));
	p += strlen(bucket);
	*p++ = (uint8_t)strlen(object);
	memcpy(p, object, strlen(object));
	p += strlen(object);
	be32enc(p, maxlen);

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

/* GET response-handling callback. */
static int
callback_get(void * cookie, uint8_t * buf, size_t buflen)
{
	struct get_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	uint32_t len;
	size_t lens = (size_t)(-1);
	uint8_t * dbuf = NULL;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen < 8)
			BAD("GET", "bogus length");

		/* Parse the packet. */
		status = be32dec(&buf[0]);
		len = be32dec(&buf[4]);

		/* Do we have the right packet length? */
		if (len != (uint32_t)(-1)) {
			if (buflen != 8 + len)
				BAD("GET", "bogus length");
		} else {
			if (buflen != 8)
				BAD("GET", "bogus length");
		}

		/* Did the operation succeed? */
		if (status == 200)
			failed = 0;

		/* Do we have data? */
		if ((failed == 0) && (len != (uint32_t)(-1))) {
			lens = len;
			dbuf = &buf[8];
		}
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, lens, dbuf);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_s3_request_range(Q, bucket, object, offset, len, callback, cookie):
 * Send a RANGE request to read ${len} bytes starting at offset ${offset} from
 * the object ${object} in the S3 bucket ${bucket} via the request queue ${Q}.
 * Invoke
 *     ${callback}(${cookie}, failed, buflen, buf)
 * upon request completion, where ${failed} is 0 on success or 1 on failure,
 * and ${buf} contains ${buflen} bytes of object data if ${failed} == 0 (note
 * that ${buflen} can be less than ${len} if the object contains fewer than
 * ${offset}+${len} bytes).
 */
int
proto_s3_request_range(struct wire_requestqueue * Q, const char * bucket,
    const char * object, uint32_t offset, uint32_t len,
    int (* callback)(void *, int, size_t, const uint8_t *), void * cookie)
{
	struct get_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate bucket and object name lengths. */
	if ((strlen(bucket) > 255) || (strlen(object) > 255)) {
		warn0("Bucket or object name is too long");
		goto err0;
	}

	/* Validate maxlen. */
	if (len > PROTO_S3_MAXLEN) {
		warn0("RANGE length is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct get_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 6 + strlen(bucket) + strlen(object) + 8;

	/* Start writing a request. */
	if ((p = rbuf =
	    wire_requestqueue_add_getbuf(Q, rlen, callback_range, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_S3_RANGE);
	p += 4;
	*p++ = (uint8_t)strlen(bucket);
	memcpy(p, bucket, strlen(bucket));
	p += strlen(bucket);
	*p++ = (uint8_t)strlen(object);
	memcpy(p, object, strlen(object));
	p += strlen(object);
	be32enc(p, offset);
	p += 4;
	be32enc(p, len);

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

/* RANGE response-handling callback. */
static int
callback_range(void * cookie, uint8_t * buf, size_t buflen)
{
	struct get_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	uint32_t len;
	size_t lens = (size_t)(-1);
	uint8_t * dbuf = NULL;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen < 8)
			BAD("RANGE", "bogus length");

		/* Parse the packet. */
		status = be32dec(&buf[0]);
		len = be32dec(&buf[4]);

		/* Do we have the right packet length? */
		if (len != (uint32_t)(-1)) {
			if (buflen != 8 + len)
				BAD("RANGE", "bogus length");
		} else {
			if (buflen != 8)
				BAD("RANGE", "bogus length");
		}

		/* Warn about HTTP 200 responses. */
		if (status == 200)
			BAD("RANGE", "HTTP 200 response");

		/* Did the operation succeed? */
		if (status == 206)
			failed = 0;

		/* Do we have data? */
		if ((failed == 0) && (len != (uint32_t)(-1))) {
			lens = len;
			dbuf = &buf[8];
		}
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, lens, dbuf);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_s3_request_head(Q, bucket, object, callback, cookie):
 * Send a HEAD request for the object ${object} in the S3 bucket ${bucket} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status, len)
 * upon request completion, where ${status} is the HTTP status code (or 0 on
 * error) and ${len} is the object size (if status == 200) or -1 (otherwise).
 */
int
proto_s3_request_head(struct wire_requestqueue * Q, const char * bucket,
    const char * object,
    int (* callback)(void *, int, size_t), void * cookie)
{
	struct head_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate bucket and object name lengths. */
	if ((strlen(bucket) > 255) || (strlen(object) > 255)) {
		warn0("Bucket or object name is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct head_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 6 + strlen(bucket) + strlen(object);

	/* Start writing a request. */
	if ((p = rbuf =
	    wire_requestqueue_add_getbuf(Q, rlen, callback_head, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_S3_HEAD);
	p += 4;
	*p++ = (uint8_t)strlen(bucket);
	memcpy(p, bucket, strlen(bucket));
	p += strlen(bucket);
	*p++ = (uint8_t)strlen(object);
	memcpy(p, object, strlen(object));

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

/* HEAD response-handling callback. */
static int
callback_head(void * cookie, uint8_t * buf, size_t buflen)
{
	struct head_cookie * C = cookie;
	uint32_t status = 0;
	uint32_t len;
	size_t lens = (size_t)(-1);
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen != 8)
			BAD("HEAD", "bogus length");

		/* Parse the packet. */
		status = be32dec(&buf[0]);
		len = be32dec(&buf[4]);

		/* If status == 200 and len != -1, we have a length. */
		if ((status == 200) && (len != (uint32_t)(-1)))
			lens = len;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, status, lens);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}


/**
 * proto_s3_request_delete(Q, bucket, object, callback, cookie):
 * Send a DELETE request for the object ${object} in the S3 bucket ${bucket}
 * via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where ${failed} is 0 on success or 1 on failure.
 */
int
proto_s3_request_delete(struct wire_requestqueue * Q, const char * bucket,
    const char * object, int (* callback)(void *, int), void * cookie)
{
	struct delete_cookie * C;
	uint8_t *rbuf, *p;
	size_t rlen;

	/* Validate bucket and object name lengths. */
	if ((strlen(bucket) > 255) || (strlen(object) > 255)) {
		warn0("Bucket or object name is too long");
		goto err0;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct delete_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Compute request packet size. */
	rlen = 6 + strlen(bucket) + strlen(object);

	/* Start writing a request. */
	if ((p = rbuf =
	    wire_requestqueue_add_getbuf(Q, rlen, callback_delete, C)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(p, PROTO_S3_DELETE);
	p += 4;
	*p++ = (uint8_t)strlen(bucket);
	memcpy(p, bucket, strlen(bucket));
	p += strlen(bucket);
	*p++ = (uint8_t)strlen(object);
	memcpy(p, object, strlen(object));

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

/* DELETE response-handling callback. */
static int
callback_delete(void * cookie, uint8_t * buf, size_t buflen)
{
	struct delete_cookie * C = cookie;
	int failed = 1;
	uint32_t status;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen != 4)
			BAD("DELETE", "bogus length");

		/* Parse the packet. */
		status = be32dec(&buf[0]);

		/* Did the operation succeed? */
		if (status == 204)
			failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

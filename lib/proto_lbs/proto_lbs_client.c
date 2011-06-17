#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "proto_lbs.h"

static int callback_params(void *, uint8_t *, size_t);
static int callback_get(void *, uint8_t *, size_t);
static int callback_append(void *, uint8_t *, size_t);
static int callback_free(void *, uint8_t *, size_t);

struct params_cookie {
	int (* callback)(void *, int, size_t, uint64_t);
	void * cookie;
	uint8_t buf[4];
};

struct get_cookie {
	int (* callback)(void *, int, int, const uint8_t *);
	void * cookie;
	size_t blklen;
	uint8_t buf[12];
};

struct append_cookie {
	int (* callback)(void *, int, int, uint64_t);
	void * cookie;
	uint8_t * buf;
};

struct free_cookie {
	int (* callback)(void *, int);
	void * cookie;
	uint8_t buf[12];
};

/* Macro for simplifying response-parsing errors. */
#define BAD(rtype, ftype)	do {				\
	warn0("Received %s response with %s", rtype, ftype);	\
	goto failed;						\
} while (0)

/**
 * proto_lbs_request_params(Q, callback, cookie):
 * Send a PARAMS request via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, blklen, blkno)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * blklen is the block size, and blkno is the next block #.
 */
int
proto_lbs_request_params(struct wire_requestqueue * Q,
    int (* callback)(void *, int, size_t, uint64_t), void * cookie)
{
	struct params_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct params_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Construct request. */
	be32enc(&C->buf[0], PROTO_LBS_PARAMS);

	/* Send request. */
	if (wire_requestqueue_add(Q, C->buf, 4, callback_params, C))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* PARAMS response-handling callback. */
static int
callback_params(void * cookie, uint8_t * buf, size_t buflen)
{
	struct params_cookie * C = cookie;
	int failed = 1;
	size_t blklen = 0;
	uint64_t blkno = 0;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Do we have the right packet length? */
		if (buflen != 12)
			BAD("PARAMS", "bogus length");

		/* Parse the packet. */
		blklen = be32dec(&buf[0]);
		blkno = be64dec(&buf[4]);

		/* We succcessfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, blklen, blkno);

	/* Free the response buffer. */
	free(buf);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_lbs_request_get(Q, blkno, blklen, callback, cookie):
 * Send a GET request to read block ${blkno} of length ${blklen} via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status, buf)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * status is 0 if the block has been read and 1 if the block does not exist,
 * and buf contains the block data.
 */
int
proto_lbs_request_get(struct wire_requestqueue * Q,
    uint64_t blkno, size_t blklen,
    int (* callback)(void *, int, int, const uint8_t *), void * cookie)
{
	struct get_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct get_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->blklen = blklen;

	/* Construct request. */
	be32enc(&C->buf[0], PROTO_LBS_GET);
	be64enc(&C->buf[4], blkno);

	/* Send request. */
	if (wire_requestqueue_add(Q, C->buf, 12, callback_get, C))
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
	int status = 0;
	const uint8_t * blk = NULL;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD("GET", "bogus length");
		if (be32dec(&buf[0]) > 1)
			BAD("GET", "bogus status code");
		status = be32dec(&buf[0]);

		/* Do we have the right packet length? */
		if ((status == 0) && (buflen != 4 + C->blklen))
			BAD("GET", "wrong length for status");
		if ((status == 1) && (buflen != 4))
			BAD("GET", "wrong length for status");

		/* Find the block data, if any. */
		if (status == 0)
			blk = &buf[4];

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, status, blk);

	/* Free the response buffer. */
	free(buf);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_lbs_request_append(Q, nblks, blkno, blklen, buf, callback, cookie):
 * Send an APPEND request to write ${nblks} ${blklen}-byte blocks, starting
 * at position ${blkno}, with data from ${buf} to the request queue ${Q}.
 * Invoke
 *    ${callback}(${cookie}, failed, status, blkno)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * status is 0 if the append completed and 1 otherwise, and blkno is the
 * next available block number. 
 */
int
proto_lbs_request_append(struct wire_requestqueue * Q,
    uint32_t nblks, uint64_t blkno, size_t blklen, const uint8_t * buf,
    int (* callback)(void *, int, int, uint64_t), void * cookie)
{
	struct append_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct append_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Allocate request buffer. */
	if ((C->buf = malloc(16 + nblks * blklen)) == NULL)
		goto err1;

	/* Construct request. */
	be32enc(&C->buf[0], PROTO_LBS_APPEND);
	be32enc(&C->buf[4], nblks);
	be64enc(&C->buf[8], blkno);
	memcpy(&C->buf[16], buf, nblks * blklen);

	/* Send request. */
	if (wire_requestqueue_add(Q, C->buf, 16 + nblks * blklen,
	    callback_append, C))
		goto err2;

	/* Success! */
	return (0);

err2:
	free(C->buf);
err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* APPEND response-handling callback. */
static int
callback_append(void * cookie, uint8_t * buf, size_t buflen)
{
	struct append_cookie * C = cookie;
	int failed = 1;
	int status = 0;
	uint64_t blkno = 0;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD("APPEND", "bogus length");
		if (be32dec(&buf[0]) > 1)
			BAD("APPEND", "bogus status code");
		status = be32dec(&buf[0]);

		/* Do we have the right packet length? */
		if ((status == 0) && (buflen != 12))
			BAD("APPEND", "wrong length for status");
		if ((status == 1) && (buflen != 4))
			BAD("APPEND", "wrong length for status");

		/* Parse the next block #, if any. */
		if (status == 0)
			blkno = be64dec(&buf[4]);

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed, status, blkno);

	/* Free the response buffer. */
	free(buf);

	/* Free the cookie, including the request buffer. */
	free(C->buf);
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * proto_lbs_request_free(Q, blkno, callback, cookie):
 * Send a FREE request to free blocks numbred less than ${blkno} to the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int
proto_lbs_request_free(struct wire_requestqueue * Q, uint64_t blkno,
    int (* callback)(void *, int), void * cookie)
{
	struct free_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct free_cookie))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;

	/* Construct request. */
	be32enc(&C->buf[0], PROTO_LBS_FREE);
	be64enc(&C->buf[4], blkno);

	/* Send request. */
	if (wire_requestqueue_add(Q, C->buf, 12, callback_free, C))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* FREE response-handling callback. */
static int
callback_free(void * cookie, uint8_t * buf, size_t buflen)
{
	struct free_cookie * C = cookie;
	int failed = 1;
	int rc;

	/* If we have a packet, parse it. */
	if (buf != NULL) {
		/* Is the status code sane? */
		if (buflen < 4)
			BAD("FREE", "bogus length");
		if (be32dec(&buf[0]) > 0)
			BAD("FREE", "bogus status code");

		/* We successfully parsed this response. */
		failed = 0;
	}

failed:
	/* Invoke the upstream callback. */
	rc = (C->callback)(C->cookie, failed);

	/* Free the response buffer. */
	free(buf);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

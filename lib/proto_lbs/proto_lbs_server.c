#include <stdlib.h>
#include <string.h>

#include "wire.h"
#include "sysendian.h"

#include "proto_lbs.h"

struct request_read {
	int (*callback)(void *, struct proto_lbs_request *);
	void * cookie;
	void * read_cookie;
};

static int gotpacket(void *, struct wire_packet *);
static int docallback(struct request_read *, struct proto_lbs_request *);

/**
 * proto_lbs_request_parse(P):
 * Parse the packet ${P} and return an LBS request structure.
 */
static struct proto_lbs_request *
proto_lbs_request_parse(const struct wire_packet * P)
{
	struct proto_lbs_request * R;

	/* Allocate LBS request structure. */
	if ((R = malloc(sizeof(struct proto_lbs_request))) == NULL)
		goto err0;
	R->ID = P->ID;

	/* Sanity-check packet length. */
	if (P->len < 4)
		goto err1;

	/* Figure out request type. */
	R->type = be32dec(&P->buf[0]);

	/* Parse packet. */
	switch (R->type) {
	case PROTO_LBS_PARAMS:
		if (P->len != 4)
			goto err1;
		/* Nothing to parse. */
		break;
	case PROTO_LBS_GET:
		if (P->len != 12)
			goto err1;
		R->r.get.blkno = be64dec(&P->buf[4]);
		break;
	case PROTO_LBS_APPEND:
		if (P->len < 16)
			goto err1;
		R->r.append.nblks = be32dec(&P->buf[4]);
		R->r.append.blkno = be64dec(&P->buf[8]);
		if (R->r.append.nblks == 0)
			goto err1;
		if ((P->len - 16) % R->r.append.nblks)
			goto err1;
		R->r.append.blklen = (P->len - 16) / R->r.append.nblks;
		if ((R->r.append.buf = malloc(P->len - 16)) == NULL)
			goto err1;
		memcpy(R->r.append.buf, &P->buf[16], P->len - 16);
		break;
	case PROTO_LBS_FREE:
		if (P->len != 12)
			goto err1;
		R->r.free.blkno = be64dec(&P->buf[4]);
		break;
	default:
		goto err1;
	}

	/* Success! */
	return (R);

err1:
	free(R);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * proto_lbs_request_read(R, callback, cookie):
 * Read a packet from the reader ${R} and parse it as an LBS request.  Invoke
 * ${callback}(${cookie}, [request]), or ${callback}(${cookie}, NULL) if a
 * request could not be read or parsed.  The callback is responsible for
 * freeing the request structure.  Return a cookie which can be used to
 * cancel the operation.
 */
void *
proto_lbs_request_read(struct netbuf_read * R,
    int (* callback)(void *, struct proto_lbs_request *), void * cookie)
{
	struct request_read * G;

	/* Bake a cookie. */
	if ((G = malloc(sizeof(struct request_read))) == NULL)
		goto err0;
	G->callback = callback;
	G->cookie = cookie;

	/* Read a packet. */
	if ((G->read_cookie = wire_readpacket(R, gotpacket, G)) == NULL)
		goto err1;

	/* Success! */
	return (G);

err1:
	free(G);
err0:
	/* Failure! */
	return (NULL);
}

/* We have a packet. */
static int
gotpacket(void * cookie, struct wire_packet * P)
{
	struct request_read * G = cookie;
	struct proto_lbs_request * R;

	/* If we have no packet, we failed. */
	if (P == NULL)
		goto failed;

	/* Parse the packet. */
	if ((R = proto_lbs_request_parse(P)) == NULL)
		goto failed1;

	/* Free the packet. */
	free(P->buf);
	free(P);

	/* Perform the callback. */
	return (docallback(G, R));

failed1:
	free(P->buf);
	free(P);
failed:
	/* Perform the callback. */
	return (docallback(G, NULL));
}

/* Do the callback and free the request_read structure. */
static int
docallback(struct request_read * G, struct proto_lbs_request * R)
{
	int rc;

	/* Do the callback. */
	rc = (G->callback)(G->cookie, R);

	/* Free the structure. */
	free(G);

	/* Pass the callback status back. */
	return (rc);
}

/**
 * proto_lbs_request_read_cancel(cookie):
 * Cancel the request read for which ${cookie} was returned.  Do not invoke
 * the callback function.
 */
void
proto_lbs_request_read_cancel(void * cookie)
{
	struct request_read * G = cookie;

	/* Cancel the read. */
	wire_readpacket_cancel(G->read_cookie);

	/* Free the cookie. */
	free(G);
}

/**
 * proto_lbs_response_params(Q, ID, blklen, blkno):
 * Send a PARAMS response with ID ${ID} to the write queue ${Q} indicating
 * that the block size is ${blklen} bytes and the next available block # is
 * ${blkno}.  Invoke ${callback}(${cookie}, 0 / 1) on packet write success /
 * failure.
 */
int
proto_lbs_response_params(struct netbuf_write * Q, uint64_t ID,
    uint32_t blklen, uint64_t blkno)
{
	struct wire_packet P;
	uint8_t buf[12];

	P.ID = ID;
	P.len = 12;
	P.buf = buf;

	/* Construct the packet. */
	be32enc(&P.buf[0], blklen);
	be64enc(&P.buf[4], blkno);

	/* Queue the packet. */
	if (wire_writepacket(Q, &P))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_get(Q, ID, status, blklen, buf, callback, cookie):
 * Send a GET response with ID ${ID} to the write queue ${Q} with status code
 * ${status} and ${blklen} bytes of data from ${buf} if ${status} is zero.
 * Invoke ${callback}(${cookie}, 0 / 1) on packet write success / failure.
 */
int
proto_lbs_response_get(struct netbuf_write * Q, uint64_t ID,
    uint32_t status, uint32_t blklen, const uint8_t * buf)
{
	struct wire_packet P;

	P.ID = ID;
	P.len = 4 + ((status == 0) ? blklen : 0);

	/* Allocate the packet buffer. */
	if ((P.buf = malloc(P.len)) == NULL)
		goto err0;

	/* Construct the packet. */
	be32enc(&P.buf[0], status);
	if (status == 0)
		memcpy(&P.buf[4], buf, blklen);

	/* Queue the packet. */
	if (wire_writepacket(Q, &P))
		goto err1;

	/* Free the packet buffer. */
	free(P.buf);

	/* Success! */
	return (0);

err1:
	free(P.buf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_append(Q, ID, status, blkno, callback, cookie):
 * Send an APPEND response with ID ${ID} to the write queue ${Q} with status
 * code ${status} and next block number ${blkno} if ${status} is zero.
 * Invoke ${callback}(${cookie}, 0 / 1) on packet write success / failure.
 */
int
proto_lbs_response_append(struct netbuf_write * Q, uint64_t ID,
    uint32_t status, uint64_t blkno)
{
	struct wire_packet P;
	uint8_t buf[12];

	P.ID = ID;
	P.len = (status == 0) ? 12 : 4;
	P.buf = buf;

	/* Construct the packet. */
	be32enc(&P.buf[0], status);
	if (status == 0)
		be64enc(&P.buf[4], blkno);

	/* Queue the packet. */
	if (wire_writepacket(Q, &P))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_free(Q, ID, callback, cookie):
 * Send a FREE response with ID ${ID} to the write queue ${Q}.  Invoke
 * ${callback}(${cookie}, 0 / 1) on packet write success / failure.
 */
int
proto_lbs_response_free(struct netbuf_write * Q, uint64_t ID)
{
	struct wire_packet P;
	uint8_t buf[4];

	P.ID = ID;
	P.len = 4;
	P.buf = buf;

	/* Construct the packet. */
	be32enc(&P.buf[0], 0);

	/* Queue the packet. */
	if (wire_writepacket(Q, &P))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

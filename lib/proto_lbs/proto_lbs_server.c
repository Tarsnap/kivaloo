#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "wire.h"
#include "sysendian.h"

#include "proto_lbs.h"

/**
 * proto_lbs_request_parse(P, R):
 * Parse the packet ${P} into the LBS request structure ${R}.
 */
static int
proto_lbs_request_parse(const struct wire_packet * P,
    struct proto_lbs_request * R)
{

	/* Sanity check. */
	assert(P->len <= UINT32_MAX);

	/* Store request ID. */
	R->ID = P->ID;

	/* Sanity-check packet length. */
	if (P->len < 4)
		goto err0;

	/* Figure out request type. */
	R->type = be32dec(&P->buf[0]);

	/* Parse packet. */
	switch (R->type) {
	case PROTO_LBS_PARAMS:
	case PROTO_LBS_PARAMS2:
		if (P->len != 4)
			goto err0;
		/* Nothing to parse. */
		break;
	case PROTO_LBS_GET:
		if (P->len != 12)
			goto err0;
		R->r.get.blkno = be64dec(&P->buf[4]);
		break;
	case PROTO_LBS_APPEND:
		if (P->len < 16)
			goto err0;
		R->r.append.nblks = be32dec(&P->buf[4]);
		R->r.append.blkno = be64dec(&P->buf[8]);
		if (R->r.append.nblks == 0)
			goto err0;
		if ((P->len - 16) % R->r.append.nblks)
			goto err0;
		R->r.append.blklen = (uint32_t)((P->len - 16) /
		    R->r.append.nblks);
		if ((R->r.append.buf = malloc(P->len - 16)) == NULL)
			goto err0;
		memcpy(R->r.append.buf, &P->buf[16], P->len - 16);
		break;
	case PROTO_LBS_FREE:
		if (P->len != 12)
			goto err0;
		R->r.free.blkno = be64dec(&P->buf[4]);
		break;
	default:
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an LBS request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_LBS_NONE.
 */
int
proto_lbs_request_read(struct netbuf_read * R, struct proto_lbs_request * req)
{
	struct wire_packet P;

	/* Try to grab a packet from the buffered reader. */
	if (wire_readpacket_peek(R, &P))
		goto err0;

	/* Do we have a packet? */
	if (P.buf == NULL)
		goto nopacket;

	/* Parse this packet. */
	if (proto_lbs_request_parse(&P, req))
		goto err0;

	/* Consume the packet. */
	wire_readpacket_consume(R, &P);

	/* Success! */
	return (0);

nopacket:
	/* Record that no request was available. */
	req->type = PROTO_LBS_NONE;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_params(Q, ID, blklen, blkno):
 * Send a PARAMS response with ID ${ID} to the write queue ${Q} indicating
 * that the block size is ${blklen} bytes and the next available block # is
 * ${blkno}.
 */
int
proto_lbs_response_params(struct netbuf_write * Q, uint64_t ID,
    uint32_t blklen, uint64_t blkno)
{
	uint8_t * wbuf;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 12)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], blklen);
	be64enc(&wbuf[4], blkno);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, 12))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_params2(Q, ID, blklen, blkno, lastblk):
 * Send a PARAMS2 response with ID ${ID} to the write queue ${Q} indicating
 * that the block size is ${blklen} bytes, the next available block # is
 * ${blkno}, and the last block written was ${lastblk}.
 */
int
proto_lbs_response_params2(struct netbuf_write * Q, uint64_t ID,
    uint32_t blklen, uint64_t blkno, uint64_t lastblk)
{
	uint8_t * wbuf;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 20)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], blklen);
	be64enc(&wbuf[4], blkno);
	be64enc(&wbuf[12], lastblk);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, 20))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_get(Q, ID, status, blklen, buf):
 * Send a GET response with ID ${ID} to the write queue ${Q} with status code
 * ${status} and ${blklen} bytes of data from ${buf} if ${status} is zero.
 */
int
proto_lbs_response_get(struct netbuf_write * Q, uint64_t ID,
    int status, uint32_t blklen, const uint8_t * buf)
{
	uint8_t * wbuf;
	size_t len;

	/* Sanity check. */
	assert((status == 0) || (status == 1));

	/* Compute the response length. */
	len = 4 + ((status == 0) ? blklen : 0);

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, len)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], (uint32_t)status);
	if (status == 0)
		memcpy(&wbuf[4], buf, blklen);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, len))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_append(Q, ID, status, blkno):
 * Send an APPEND response with ID ${ID} to the write queue ${Q} with status
 * code ${status} and next block number ${blkno} if ${status} is zero.
 */
int
proto_lbs_response_append(struct netbuf_write * Q, uint64_t ID,
    int status, uint64_t blkno)
{
	uint8_t * wbuf;
	size_t len;

	/* Sanity check. */
	assert((status == 0) || (status == 1));

	/* Compute the response length. */
	len = (status == 0) ? 12 : 4;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, len)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], (uint32_t)status);
	if (status == 0)
		be64enc(&wbuf[4], blkno);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, len))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_lbs_response_free(Q, ID):
 * Send a FREE response with ID ${ID} to the write queue ${Q}.
 */
int
proto_lbs_response_free(struct netbuf_write * Q, uint64_t ID)
{
	uint8_t * wbuf;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 4)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], 0);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, 4))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

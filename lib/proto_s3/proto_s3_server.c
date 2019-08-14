#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "wire.h"

#include "proto_s3.h"

/* Parse a length-prefixed string and increment pos. */
static char *
mkstr(uint8_t * buf, size_t len, size_t *pos)
{
	char * s;
	size_t slen;
	size_t i;

	/* Extract the string length. */
	if (len < *pos + 1)
		goto err0;
	slen = buf[*pos];
	*pos += 1;

	/* Make sure the string is not prematurely terminated. */
	if (len < *pos + slen)
		goto err0;
	for (i = 0; i < slen; i++)
		if (buf[*pos + i] == '\0')
			goto err0;

	/* Allocate space for string. */
	if ((s = malloc(slen + 1)) == NULL)
		goto err0;

	/* Copy and NUL-terminate string. */
	memcpy(s, &buf[*pos], slen);
	*pos += slen;
	s[slen] = '\0';

	/* Success! */
	return (s);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * proto_s3_request_parse(P, R):
 * Parse the packet ${P} into the S3 request structure ${R}.
 */
static int
proto_s3_request_parse(const struct wire_packet * P,
    struct proto_s3_request * R)
{
	size_t pos = 0;

	/* Store request ID. */
	R->ID = P->ID;

	/* Extract the request type. */
	if (P->len < pos + 4)
		goto err0;
	R->type = be32dec(&P->buf[pos]);
	pos += 4;

	/* Extract bucket name (appears in every request type). */
	if ((R->bucket = mkstr(P->buf, P->len, &pos)) == NULL)
		goto err0;

	/* Extract object name (appears in every request type). */
	if ((R->object = mkstr(P->buf, P->len, &pos)) == NULL)
		goto err1;

	/* Parse request-type-specific fields. */
	switch (R->type) {
	case PROTO_S3_PUT:
		if (P->len < pos + 4)
			goto err2;
		R->r.put.len = be32dec(&P->buf[pos]);
		pos += 4;
		if (P->len != pos + R->r.put.len)
			goto err2;
		if ((R->r.put.buf = malloc(R->r.put.len)) == NULL)
			goto err2;
		memcpy(R->r.put.buf, &P->buf[pos], R->r.put.len);
		break;
	case PROTO_S3_GET:
		if (P->len != pos + 4)
			goto err2;
		R->r.get.maxlen = be32dec(&P->buf[pos]);
		break;
	case PROTO_S3_RANGE:
		if (P->len != pos + 8)
			goto err2;
		R->r.range.offset = be32dec(&P->buf[pos]);
		R->r.range.len = be32dec(&P->buf[pos + 4]);
		break;
	case PROTO_S3_HEAD:
	case PROTO_S3_DELETE:
		if (P->len != pos)
			goto err2;
		break;
	default:
		goto err2;
	}

	/* Success! */
	return (0);

err2:
	free(R->object);
err1:
	free(R->bucket);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_s3_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an S3 request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_S3_NONE.
 */
int
proto_s3_request_read(struct netbuf_read * R, struct proto_s3_request * req)
{
	struct wire_packet P;

	/* Try to grab a packet from the buffered reader. */
	if (wire_readpacket_peek(R, &P))
		goto err0;

	/* Do we have a packet? */
	if (P.buf == NULL)
		goto nopacket;

	/* Parse this packet. */
	if (proto_s3_request_parse(&P, req))
		goto err0;

	/* Consume the packet. */
	wire_readpacket_consume(R, &P);

	/* Success! */
	return (0);

nopacket:
	/* Record that no request was available. */
	req->type = PROTO_S3_NONE;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_s3_request_free(req):
 * Free the contents of the S3 request structure ${req}.
 */
void
proto_s3_request_free(struct proto_s3_request * req)
{

	/* If this is a PUT, free the malloced data buffer. */
	if (req->type == PROTO_S3_PUT)
		free(req->r.put.buf);

	/* Free the object and bucket names. */
	free(req->object);
	free(req->bucket);
}

/**
 * proto_s3_response_status(Q, ID, status):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the S3 request completed with HTTP status code ${status}.
 */
int
proto_s3_response_status(struct netbuf_write * Q, uint64_t ID, int status)
{
	uint8_t * wbuf;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 4)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], status);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, 4))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}


/**
 * proto_s3_response_data(Q, ID, status, len, buf):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the S3 request completed with HTTP status code ${status} (or 0 on error)
 * and the returned data was ${len} bytes from ${buf}.  If ${buf} is NULL,
 * send the length ${len} but no data.
 */
int
proto_s3_response_data(struct netbuf_write * Q, uint64_t ID, int status,
    uint32_t len, const uint8_t * buf)
{
	uint8_t * wbuf;
	size_t rlen;

	/* Compute the response length. */
	rlen = 8 + (((buf != NULL) && (len != (uint32_t)(-1))) ? len : 0);

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, rlen)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], status);
	be32enc(&wbuf[4], len);
	if ((buf != NULL) && (len != (uint32_t)(-1)))
		memcpy(&wbuf[8], buf, len);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, rlen))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

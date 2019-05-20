#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "wire.h"

#include "proto_dynamodb_kv.h"

/**
 * proto_dynamodb_kv_request_parse(P, R):
 * Parse the packet ${P} into the DynamoDB-KV request structure ${R}.
 */
static int
proto_dynamodb_kv_request_parse(const struct wire_packet * P,
    struct proto_ddbkv_request * R)
{
	size_t pos = 0;
	size_t buflen;
	size_t i;

	/* Store request ID. */
	R->ID = P->ID;

	/* Initialize pointers to NULL to make cleanup easier. */
	R->key = NULL;
	R->buf = NULL;

	/* Extract the request type. */
	if (P->len < pos + 4)
		goto err0;
	R->type = be32dec(&P->buf[pos]);
	pos += 4;

	/* Extract key length (appears in every request type). */
	if (P->len < pos + 1)
		goto err0;
	buflen = P->buf[pos++];

	/* Sanity-check key. */
	if (P->len < pos + buflen)
		goto err0;
	for (i = 0; i < buflen; i++)
		if (P->buf[pos + i] == '\0')
			goto err0;

	/* Extract key into newly allocated buffer. */
	if ((R->key = malloc(buflen + 1)) == NULL)
		goto err0;
	memcpy(R->key, &P->buf[pos], buflen);
	R->key[buflen] = '\0';
	pos += buflen;

	/* PUT requests have a value too. */
	switch (R->type) {
	case PROTO_DDBKV_PUT:
		if (P->len < pos + 4)
			goto err1;
		R->len = be32dec(&P->buf[pos]);
		pos += 4;
		if (P->len < pos + R->len)
			goto err1;
		if ((R->buf = malloc(R->len)) == NULL)
			goto err1;
		memcpy(R->buf, &P->buf[pos], R->len);
		pos += R->len;
		break;
	case PROTO_DDBKV_GET:
	case PROTO_DDBKV_GETC:
	case PROTO_DDBKV_DELETE:
		break;
	default:
		goto err1;
	}

	/* Check that we processed the entire request record. */
	if (P->len != pos)
		goto err1;

	/* Success! */
	return (0);

err1:
	free(R->buf);
	free(R->key);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_dynamodb_kv_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as a DynamoDB-KV request.
 * Return the parsed request via ${req}.  If no request is available, return
 * with ${req}->type == PROTO_DDBKV_NONE.
 */
int
proto_dynamodb_kv_request_read(struct netbuf_read * R,
    struct proto_ddbkv_request * req)
{
	struct wire_packet P;

	/* Try to grab a packet from the buffered reader. */
	if (wire_readpacket_peek(R, &P))
		goto err0;

	/* Do we have a packet? */
	if (P.buf == NULL)
		goto nopacket;

	/* Parse this packet. */
	if (proto_dynamodb_kv_request_parse(&P, req))
		goto err0;

	/* Consume the packet. */
	wire_readpacket_consume(R, &P);

	/* Success! */
	return (0);

nopacket:
	/* Record that no request was available. */
	req->type = PROTO_DDBKV_NONE;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_dynamodb_kv_request_free(req):
 * Free the contents of the DynamoDB-KV request structure ${req}.  The
 * structure itself is not freed.
 */
void
proto_dynamodb_kv_request_free(struct proto_ddbkv_request * req)
{

	/* If this is a PUT, free the malloced data buffer. */
	if (req->type == PROTO_DDBKV_PUT)
		free(req->buf);

	/* Free the key. */
	free(req->key);
}

/**
 * proto_dynamodb_kv_response_status(Q, ID, status):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the DynamoDB request completed successfully (${status} = 0) or failed
 * (${status} = 1).
 */
int
proto_dynamodb_kv_response_status(struct netbuf_write * Q, uint64_t ID,
    int status)
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
 * proto_dynamodb_kv_response_data(Q, ID, status, len, buf):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the DynamoDB request completed successfully (${status} = 0) with the
 * provided data, failed (${status} = 1), or returned no data (${status} = 2).
 */
int
proto_dynamodb_kv_response_data(struct netbuf_write * Q, uint64_t ID,
    int status, uint32_t len, const uint8_t * buf)
{
	uint8_t * wbuf;
	size_t rlen;

	/* Compute the response length. */
	rlen = 4 + ((status == 0) ? len + 4 : 0);

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, rlen)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], status);
	if (status == 0) {
		be32enc(&wbuf[4], len);
		memcpy(&wbuf[8], buf, len);
	}

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, rlen))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

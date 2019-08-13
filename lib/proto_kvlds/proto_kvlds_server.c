#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kvldskey.h"
#include "mpool.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "proto_kvlds.h"

MPOOL(request, struct proto_kvlds_request, 4096);

/**
 * proto_kvlds_request_parse(P, R):
 * Parse the packet ${P} into the KVLDS request structure ${R}.
 */
static int
proto_kvlds_request_parse(const struct wire_packet * P,
    struct proto_kvlds_request * R)
{
	size_t bufpos;

	/* Store request ID. */
	R->ID = P->ID;

	/* Initialize keys to NULL. */
	R->key = R->oval = R->value = NULL;

	/* Sanity-check packet length. */
	if (P->len < 4)
		goto err0;
	if (P->len > sizeof(R->blob))
		goto err0;

	/* Copy the packet data into the request structure. */
	memcpy(R->blob, P->buf, P->len);

	/* Figure out request type. */
	R->type = be32dec(&R->blob[0]);
	bufpos = 4;

/* Macro for extracting a key and advancing the buffer position. */
#define GRABKEY(dest, buf, buflen, bufpos, invalid) do {	\
	if (bufpos == buflen)					\
		goto invalid;					\
	dest = (struct kvldskey *)&buf[bufpos];			\
	bufpos += kvldskey_serial_size(dest);			\
	if (bufpos > buflen)					\
		goto invalid;					\
} while (0)

	/* Parse packet. */
	switch (R->type) {
	case PROTO_KVLDS_PARAMS:
		/* Nothing to parse. */
		break;
	case PROTO_KVLDS_DELETE:
	case PROTO_KVLDS_GET:
		/* Parse key. */
		GRABKEY(R->key, R->blob, P->len, bufpos, err1);
		break;
	case PROTO_KVLDS_SET:
	case PROTO_KVLDS_ADD:
	case PROTO_KVLDS_MODIFY:
		/* Parse key. */
		GRABKEY(R->key, R->blob, P->len, bufpos, err1);

		/* Parse value. */
		GRABKEY(R->value, R->blob, P->len, bufpos, err1);
		break;
	case PROTO_KVLDS_CAD:
		/* Parse key. */
		GRABKEY(R->key, R->blob, P->len, bufpos, err1);

		/* Parse oval. */
		GRABKEY(R->oval, R->blob, P->len, bufpos, err1);
		break;
	case PROTO_KVLDS_CAS:
		/* Parse key. */
		GRABKEY(R->key, R->blob, P->len, bufpos, err1);

		/* Parse oval. */
		GRABKEY(R->oval, R->blob, P->len, bufpos, err1);

		/* Parse value. */
		GRABKEY(R->value, R->blob, P->len, bufpos, err1);
		break;
	case PROTO_KVLDS_RANGE:
		/* Parse maximum key-value pairs length. */
		if (P->len - bufpos < 4) {
			errno = 0;
			goto err1;
		}
		R->range_max = be32dec(&R->blob[bufpos]);
		bufpos += 4;

		/* Parse start key. */
		GRABKEY(R->range_start, R->blob, P->len, bufpos, err1);

		/* Parse end key. */
		GRABKEY(R->range_end, R->blob, P->len, bufpos, err1);
		break;
	default:
		warn0("Unrecognized request type received: 0x%08" PRIx32,
		    R->type);
		goto err1;
	}

	/* Did we reach the end of the packet? */
	if (bufpos != P->len)
		goto err1;

	/* Success! */
	return (0);

err1:
	warnp("Error parsing request packet of type 0x%08" PRIx32, R->type);
err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_alloc(void):
 * Allocate a struct proto_kvlds_request.
 */
struct proto_kvlds_request *
proto_kvlds_request_alloc(void)
{

	return (mpool_request_malloc());
}

/**
 * proto_kvlds_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an KVLDS request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_KVLDS_NONE.
 */
int
proto_kvlds_request_read(struct netbuf_read * R,
    struct proto_kvlds_request * req)
{
	struct wire_packet P;

	/* Try to grab a packet from the buffered reader. */
	if (wire_readpacket_peek(R, &P))
		goto err0;

	/* Do we have a packet? */
	if (P.buf == NULL)
		goto nopacket;

	/* Parse this packet. */
	if (proto_kvlds_request_parse(&P, req))
		goto err0;

	/* Consume the packet. */
	wire_readpacket_consume(R, &P);

	/* Success! */
	return (0);

nopacket:
	/* Record that no request was available. */
	req->type = PROTO_KVLDS_NONE;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_request_free(req):
 * Free the struct proto_kvlds_request ${req}.
 */
void
proto_kvlds_request_free(struct proto_kvlds_request * req)
{

	mpool_request_free(req);
}

/**
 * proto_kvlds_response_params(Q, ID, kmax, vmax):
 * Send a PARAMS response with ID ${ID} specifying that the maximum key
 * length is ${kmax} bytes and the maximum value length is ${vmax} bytes
 * to the write queue ${Q}.
 */
int
proto_kvlds_response_params(struct netbuf_write * Q, uint64_t ID,
    uint32_t kmax, uint32_t vmax)
{
	uint8_t * wbuf;

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 8)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], kmax);
	be32enc(&wbuf[4], vmax);

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, 8))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * proto_kvlds_response_status(Q, ID, status):
 * Send a SET/CAS/ADD/MODIFY/DELETE/CAD response with ID ${ID} and status
 * ${status} to the write queue ${Q} indicating that the request has been
 * completed with the specified status.
 */
int
proto_kvlds_response_status(struct netbuf_write * Q, uint64_t ID,
    int status)
{
	uint8_t * wbuf;

	/* Sanity check. */
	assert((status == 0) || (status == 1));

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, 4)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], (uint32_t)status);

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
 * proto_kvlds_response_get(Q, ID, status, value):
 * Send a GET response with ID ${ID}, status ${status}, and value ${value}
 * (if ${status} == 0) to the write queue ${Q} indicating that the provided
 * key is associated with the specified data (or not).
 */
int
proto_kvlds_response_get(struct netbuf_write * Q, uint64_t ID,
    int status, const struct kvldskey * value)
{
	uint8_t * wbuf;
	size_t len;

	/* Sanity check. */
	assert((status == 0) || (status == 1));

	/* Compute the response length. */
	len = 4;
	if (status == 0)
		len += kvldskey_serial_size(value);

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, len)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], (uint32_t)status);
	if (status == 0)
		kvldskey_serialize(value, &wbuf[4]);

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
 * proto_kvlds_response_range(Q, ID, nkeys, next, keys, values):
 * Send a RANGE response with ID ${ID}, next key ${next} and ${nkeys}
 * key-value pairs with the keys in ${keys} and values in ${values} to the
 * write queue ${Q}.
 */
int
proto_kvlds_response_range(struct netbuf_write * Q, uint64_t ID,
    size_t nkeys, const struct kvldskey * next,
    struct kvldskey ** keys, struct kvldskey ** values)
{
	uint8_t * wbuf;
	size_t len;
	size_t i;
	size_t bufpos;

	/* Sanity check: We can't return more than 2^32-1 keys. */
	assert(nkeys <= UINT32_MAX);

	/* Figure out how long the packet will be. */
	len = 8;
	len += kvldskey_serial_size(next);
	for (i = 0; i < nkeys; i++) {
		len += kvldskey_serial_size(keys[i]);
		len += kvldskey_serial_size(values[i]);
	}

	/* Get a packet data buffer. */
	if ((wbuf = wire_writepacket_getbuf(Q, ID, len)) == NULL)
		goto err0;

	/* Write the packet data. */
	be32enc(&wbuf[0], 0);
	be32enc(&wbuf[4], (uint32_t)nkeys);
	bufpos = 8;
	kvldskey_serialize(next, &wbuf[bufpos]);
	bufpos += kvldskey_serial_size(next);
	for (i = 0; i < nkeys; i++) {
		kvldskey_serialize(keys[i], &wbuf[bufpos]);
		bufpos += kvldskey_serial_size(keys[i]);
		kvldskey_serialize(values[i], &wbuf[bufpos]);
		bufpos += kvldskey_serial_size(values[i]);
	}

	/* Finish the packet. */
	if (wire_writepacket_done(Q, wbuf, len))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

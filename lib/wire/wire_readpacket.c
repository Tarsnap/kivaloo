#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crc32c.h"
#include "events.h"
#include "mpool.h"
#include "netbuf.h"
#include "sysendian.h"
#include "warnp.h"

#include "wire.h"

struct read_cookie {
	int (* callback)(void *, struct wire_packet *);
	void * cookie;
	struct netbuf_read * R;
	struct wire_packet * packet;
	void * wait_cookie;
};

struct wait_cookie {
	struct netbuf_read * R;
	int (* callback)(void *, int);
	void * cookie;
};

MPOOL(read_cookie, struct read_cookie, 16);
MPOOL(wait_cookie, struct wait_cookie, 16);

static int callback_wait_gotheader(void *, int);
static int callback_wait_gotdata(void *, int);
static int callback_read_gotpacket(void *, int);

/**
 * wire_readpacket_peek(R, P):
 * Look to see if a packet is available from the buffered reader ${R}.  If
 * yes, store it in the packet structure ${P}; otherwise, set ${P}->buf to
 * NULL.  On error (including if a corrupt packet is received) return -1.
 */
int
wire_readpacket_peek(struct netbuf_read * R, struct wire_packet * P)
{
	CRC32C_CTX ctx;
	uint8_t * data;
	size_t datalen;
	uint8_t cbuf[4];
	size_t i;

	/* No packet data yet. */
	P->buf = NULL;

	/* Ask the buffered reader what it has. */
	netbuf_read_peek(R, &data, &datalen);

	/* If we have less than 20 bytes, we don't have a complete packet. */
	if (datalen < 20)
		goto nopacket;

	/* Verify the header checksum. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, data, 12);
	CRC32C_Final(cbuf, &ctx);
	if (memcmp(&data[12], cbuf, 4)) {
		warn0("Incorrect CRC on packet header");
		goto failed;
	}

	/* Parse ID and length. */
	P->ID = be64dec(&data[0]);
	P->len = be32dec(&data[8]);

	/* Make sure the length is reasonable. */
	if (P->len > SIZE_MAX - 20)
		goto failed;

	/* Do we have the complete packet? */
	if (datalen < P->len + 20)
		goto nopacket;

	/* Verify the data checksum. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, &data[16], P->len);
	CRC32C_Final(cbuf, &ctx);
	for (i = 0; i < 4; i++)
		cbuf[i] ^= data[16 + P->len + i];
	if (memcmp(&data[12], cbuf, 4)) {
		warn0("Incorrect CRC on packet data");
		goto failed;
	}

	/* Point at the data. */
	P->buf = &data[16];

nopacket:
	/* Success! */
	return (0);

failed:
	/* Failure! */
	return (-1);
}

/**
 * wire_readpacket_wait(R, callback, cookie):
 * Wait until a packet is available to be read from ${R} or a failure occurs
 * while reading (e.g., EOF); then invoke ${callback}(${cookie}, status) where
 * status is 0 on success or 1 on error.  Return a cookie which can be passed
 * to wire_readpacket_wait_cancel.
 */
void *
wire_readpacket_wait(struct netbuf_read * R,
    int (* callback)(void *, int), void * cookie)
{
	struct wait_cookie * W;

	/* Bake a cookie. */
	if ((W = mpool_wait_cookie_malloc()) == NULL)
		goto err0;
	W->R = R;
	W->callback = callback;
	W->cookie = cookie;

	/* Wait for a header to be available. */
	if (netbuf_read_wait(W->R, 16, callback_wait_gotheader, W))
		goto err0;

	/* Success! */
	return (W);

err0:
	/* Failure! */
	return (NULL);
}

/* A header is available; parse it and wait for the entire packet. */
static int
callback_wait_gotheader(void * cookie, int status)
{
	CRC32C_CTX ctx;
	struct wait_cookie * W = cookie;
	uint8_t * data;
	size_t datalen;
	size_t len;
	uint8_t cbuf[4];
	int rc;

	/* Did we fail? */
	if (status)
		goto failed;

	/* Grab the header and verify the checksum. */
	netbuf_read_peek(W->R, &data, &datalen);
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, data, 12);
	CRC32C_Final(cbuf, &ctx);
	if (memcmp(&data[12], cbuf, 4)) {
		warn0("Incorrect CRC on packet header");
		goto failed;
	}

	/* Parse length and wait for the complete packet to be available. */
	len = be32dec(&data[8]);
	if (len > SIZE_MAX - 20)
		goto failed;
	if (netbuf_read_wait(W->R, len + 20, callback_wait_gotdata, W))
		goto failed;

	/* Success! */
	return (0);

failed:
	/* Perform a failure callback. */
	rc = (W->callback)(W->cookie, 1);

	/* Free the cookie. */
	mpool_wait_cookie_free(W);

	/* Return status from callback. */
	return (rc);
}

/* A packet is available; invoke the upstream callback. */
static int
callback_wait_gotdata(void * cookie, int status)
{
	struct wait_cookie * W = cookie;
	int rc;

	/* Invoke the upstream callback. */
	rc = (W->callback)(W->cookie, status);

	/* Free the cookie. */
	mpool_wait_cookie_free(W);

	/* Return status from callback. */
	return (rc);
}

/**
 * wire_readpacket_wait_cancel(cookie):
 * Cancel the packet wait for which ${cookie} was returned.  Do not invoke
 * the packet wait callback.
 */
void
wire_readpacket_wait_cancel(void * cookie)
{
	struct wait_cookie * W = cookie;

	/* Cancel the buffered reader wait. */
	netbuf_read_wait_cancel(W->R);

	/* Free the cookie. */
	mpool_wait_cookie_free(W);
}

/**
 * wire_readpacket_consume(R, P):
 * Consume from the reader ${R} the packet ${P}, which it must have returned
 * via wire_readpacket_peek.
 */
void
wire_readpacket_consume(struct netbuf_read * R, struct wire_packet * P)
{

	/* Consume the packet. */
	netbuf_read_consume(R, P->len + 20);
}

/**
 * wire_readpacket(R, callback, cookie):
 * Read a packet from the buffered reader ${R}.  When a packet has been read,
 * invoke ${callback}(${cookie}, packet); if a failure occurs while reading
 * (e.g., EOF) then invoke the callback with packet == NULL.  The callback is
 * responsible for freeing the provided packet.  Return a cookie which can be
 * passed to wire_readpacket_cancel.
 */
void *
wire_readpacket(struct netbuf_read * R,
    int (* callback)(void *, struct wire_packet *), void * cookie)
{
	struct read_cookie * RP;

	/* Bake a cookie. */
	if ((RP = mpool_read_cookie_malloc()) == NULL)
		goto err0;
	RP->callback = callback;
	RP->cookie = cookie;
	RP->R = R;

	/* Allocate a packet structure. */
	if ((RP->packet = wire_packet_malloc()) == NULL)
		goto err1;

	/* Wait for a packet to be ready. */
	if ((RP->wait_cookie =
	    wire_readpacket_wait(RP->R, callback_read_gotpacket, RP)) == NULL)
		goto err2;

	/* Success! */
	return (RP);

err2:
	wire_packet_free(RP->packet);
err1:
	mpool_read_cookie_free(RP);
err0:
	/* Failure! */
	return (NULL);
}

/* A packet is ready.  Read it and pass it to the callback. */
static int
callback_read_gotpacket(void * cookie, int status)
{
	struct read_cookie * RP = cookie;
	uint8_t * buf;
	int rc;

	/* Did we fail? */
	if (status)
		goto failed;

	/* Try to read a packet. */
	if (wire_readpacket_peek(RP->R, RP->packet))
		goto failed;

	/* Sanity-check: We should have a packet. */
	assert(RP->packet->buf != NULL);

	/* Copy the packet buffer: Our callback will own it. */
	if ((buf = malloc(RP->packet->len)) == NULL)
		goto failed;
	memcpy(buf, RP->packet->buf, RP->packet->len);
	RP->packet->buf = buf;

	/* Consume the packet. */
	wire_readpacket_consume(RP->R, RP->packet);

	/* We're ready to invoke the callback. */
	goto docallback;

failed:
	/* Free the embryonic packet; we won't pass it to the callback. */
	wire_packet_free(RP->packet);
	RP->packet = NULL;

docallback:
	/* Invoke the callback. */
	rc = (RP->callback)(RP->cookie, RP->packet);

	/* Free the cookie. */
	mpool_read_cookie_free(RP);

	/* Return status from callback. */
	return (rc);
}

/**
 * wire_readpacket_cancel(cookie):
 * Cancel the packet read for which ${cookie} was returned.  Do not invoke
 * the packet read callback.
 */
void
wire_readpacket_cancel(void * cookie)
{
	struct read_cookie * RP = cookie;

	/* Stop waiting for a packet to arrive. */
	wire_readpacket_wait_cancel(RP->wait_cookie);

	/* Free the embryonic packet. */
	wire_packet_free(RP->packet);

	/* Free the cookie. */
	mpool_read_cookie_free(RP);
}

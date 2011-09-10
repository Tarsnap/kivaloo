#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crc32c.h"
#include "mpool.h"
#include "netbuf.h"
#include "sysendian.h"
#include "warnp.h"

#include "wire.h"

struct readpacket {
	int (*callback)(void *, struct wire_packet *);
	void * cookie;
	struct netbuf_read * R;
	struct wire_packet * packet;
	uint8_t hbuf[16];
};

MPOOL(readpacket, struct readpacket, 16);

static int docallback(struct readpacket *, struct wire_packet *);
static int readheader(void *, int);
static int readrec(void *, int);

/* Do the callback and free the cookie. */
static int
docallback(struct readpacket * RP, struct wire_packet * packet)
{
	int rc;

	/* Invoke the callback. */
	rc = (RP->callback)(RP->cookie, packet);

	/* If we didn't pass the packet upstream, free it. */
	if (packet == NULL) {
		free(RP->packet->buf);
		wire_packet_free(RP->packet);
	}

	/* Free the cookie. */
	mpool_readpacket_free(RP);

	/* Return the status from the upstream callback. */
	return (rc);
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
	struct readpacket * RP;

	/* Allocate a structure and record parameters. */
	if ((RP = mpool_readpacket_malloc()) == NULL)
		goto err0;
	RP->callback = callback;
	RP->cookie = cookie;
	RP->R = R;

	/* Allocate a packet structure with no packet buffer yet. */
	if ((RP->packet = wire_packet_malloc()) == NULL)
		goto err1;
	RP->packet->buf = NULL;

	/* Attempt to read the request ID and record length. */
	if (netbuf_read_read(RP->R, RP->hbuf, 16, readheader, RP))
		goto err2;

	/* Success! */
	return (RP);

err2:
	wire_packet_free(RP->packet);
err1:
	mpool_readpacket_free(RP);
err0:
	/* Failure! */
	return (NULL);
}

/* We've got the header.  Validate and read the rest of the packet. */
static int
readheader(void * cookie, int status)
{
	struct readpacket * RP = cookie;
	CRC32C_CTX ctx;
	uint8_t cbuf[4];

	/* If status is non-zero, we failed. */
	if (status)
		goto failed;

	/* Check the CRC. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, RP->hbuf, 12);
	CRC32C_Final(cbuf, &ctx);
	if (memcmp(&RP->hbuf[12], cbuf, 4)) {
		warn0("Incorrect CRC on header");
		goto failed;
	}

	/* Copy ID and record length. */
	RP->packet->ID = be64dec(&RP->hbuf[0]);
	RP->packet->len = be32dec(&RP->hbuf[8]);

	/* Allocate record buffer, plus an extra 4 bytes to hold CRC. */
	if (RP->packet->len > SIZE_MAX - 4) {
		errno = ENOMEM;
		goto failed;
	}
	if ((RP->packet->buf = malloc(RP->packet->len + 4)) == NULL)
		goto failed;

	/* Read the record plus CRC. */
	if (netbuf_read_read(RP->R, RP->packet->buf,
	    RP->packet->len + 4, readrec, RP))
		goto failed;

	/* Success! */
	return (0);

failed:
	/* Perform the callback. */
	return (docallback(RP, NULL));
}

/* We've got the entire packet.  Validate and pass upstream. */
static int
readrec(void * cookie, int status)
{
	struct readpacket * RP = cookie;
	CRC32C_CTX ctx;
	uint8_t cbuf[4];
	size_t i;

	/* If status is non-zero, we failed. */
	if (status)
		goto failed;

	/* Check the CRC. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, RP->packet->buf, RP->packet->len);
	CRC32C_Final(cbuf, &ctx);
	for (i = 0; i < 4; i++)
		cbuf[i] ^= RP->packet->buf[RP->packet->len + i];
	if (memcmp(&RP->hbuf[12], cbuf, 4)) {
		warn0("Incorrect CRC on data");
		goto failed;
	}

	/* Perform the callback. */
	return (docallback(RP, RP->packet));

failed:
	/* Perform the callback. */
	return (docallback(RP, NULL));
}

/**
 * wire_readpacket_cancel(cookie):
 * Cancel the packet read for which ${cookie} was returned.  Do not invoke
 * the packet read callback.
 */
void
wire_readpacket_cancel(void * cookie)
{
	struct readpacket * RP = cookie;

	/* Cancel the read. */
	netbuf_read_cancel(RP->R);

	/* Free the packet and cookie. */
	free(RP->packet->buf);
	wire_packet_free(RP->packet);
	mpool_readpacket_free(RP);
}

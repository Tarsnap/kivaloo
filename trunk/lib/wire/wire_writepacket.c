#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crc32c.h"
#include "mpool.h"
#include "netbuf.h"
#include "sysendian.h"
#include "warnp.h"

#include "wire.h"

struct writepacket {
	int (*callback)(void *, int);
	void * cookie;
	const struct wire_packet * packet;
	uint8_t hbuf[16];
	uint8_t tbuf[4];
};

MPOOL(writepacket, struct writepacket, 4096);

static int writdone(void *, int);

/**
 * wire_writepacket(W, packet, callback, cookie):
 * Write the packet ${packet} to the buffered writer ${W}.  When the packet
 * has been written, invoke ${callback}(${cookie}, 0); if a failure occurs,
 * invoke the callback with 0 replaced by 1.  The packet must remain valid
 * until the callback is invoked.
 */
int
wire_writepacket(struct netbuf_write * W, const struct wire_packet * packet,
    int (* callback)(void *, int), void * cookie)
{
	struct writepacket * WP;
	CRC32C_CTX ctx;
	uint8_t cbuf[4];
	size_t i;

	/* Sanity-check packet length. */
	if (packet->len > UINT32_MAX) {
		warn0("Packet too long (%zu bytes)", packet->len);
		goto err0;
	}

	/* Bake a cookie. */
	if ((WP = mpool_writepacket_malloc()) == NULL)
		goto err0;
	WP->callback = callback;
	WP->cookie = cookie;
	WP->packet = packet;

	/* Construct header. */
	be64enc(&WP->hbuf[0], WP->packet->ID);
	be32enc(&WP->hbuf[8], WP->packet->len);
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, WP->hbuf, 12);
	CRC32C_Final(&WP->hbuf[12], &ctx);

	/* Construct trailer. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, WP->packet->buf, WP->packet->len);
	CRC32C_Final(cbuf, &ctx);
	for (i = 0; i < 4; i++)
		WP->tbuf[i] = cbuf[i] ^ WP->hbuf[12+i];

	/* Send the header. */
	if (netbuf_write_write(W, WP->hbuf, 16, writdone, NULL))
		goto err1;
	if (netbuf_write_write(W, WP->packet->buf, WP->packet->len,
	    writdone, NULL))
		goto err0;
	if (netbuf_write_write(W, WP->tbuf, 4, writdone, WP))
		goto err0;

	/* Success! */
	return (0);

err1:
	mpool_writepacket_free(WP);
err0:
	/* Failure! */
	return (-1);
}

/* Some data has been written. */
static int
writdone(void * cookie, int status)
{
	struct writepacket * WP = cookie;
	int rc;

	/* Return immediately if we have no cookie. */
	if (WP == NULL)
		return (0);

	/* Invoke the upstream callback. */
	rc = (WP->callback)(WP->cookie, status);

	/* Free the cookie. */
	mpool_writepacket_free(WP);

	/* Return the status code from the upstream callback. */
	return (rc);
}

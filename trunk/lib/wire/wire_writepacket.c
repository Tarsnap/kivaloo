#include <assert.h>
#include <stdint.h>

#include "crc32c.h"
#include "netbuf.h"
#include "sysendian.h"

#include "wire.h"

/**
 * wire_writepacket(W, packet):
 * Write the packet ${packet} to the buffered writer ${W}.
 */
int
wire_writepacket(struct netbuf_write * W, const struct wire_packet * packet)
{
	CRC32C_CTX ctx;
	uint8_t hbuf[16];
	uint8_t cbuf[4];
	uint8_t tbuf[4];
	size_t i;

	/* Sanity-check packet length. */
	assert(packet->len <= UINT32_MAX);

	/* Construct header. */
	be64enc(&hbuf[0], packet->ID);
	be32enc(&hbuf[8], packet->len);
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, hbuf, 12);
	CRC32C_Final(&hbuf[12], &ctx);

	/* Construct trailer. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, packet->buf, packet->len);
	CRC32C_Final(cbuf, &ctx);
	for (i = 0; i < 4; i++)
		tbuf[i] = cbuf[i] ^ hbuf[12+i];

	/* Send the header. */
	if (netbuf_write_write(W, hbuf, 16))
		goto err0;
	if (netbuf_write_write(W, packet->buf, packet->len))
		goto err0;
	if (netbuf_write_write(W, tbuf, 4))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "crc32c.h"
#include "netbuf.h"
#include "sysendian.h"

#include "wire.h"

/**
 * wire_writepacket_getbuf(W, ID, len):
 * Start writing a packet with ID ${ID} and data length ${len} to the buffered
 * writer ${W}.  Return a pointer to where the data should be written.  This
 * must be followed by a call to wire_writepacket_done().
 */
uint8_t *
wire_writepacket_getbuf(struct netbuf_write * W, uint64_t ID, size_t len)
{
	CRC32C_CTX ctx;
	uint8_t * wbuf;

	/* Sanity-check packet length. */
	assert(len <= UINT32_MAX);
	assert(len <= SIZE_MAX - 20);

	/* Reserve space to write the serialized packet into. */
	if ((wbuf = netbuf_write_reserve(W, len + 20)) == NULL)
		goto err0;

	/* Construct the header in-place. */
	be64enc(&wbuf[0], ID);
	be32enc(&wbuf[8], (uint32_t)len);
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, wbuf, 12);
	CRC32C_Final(&wbuf[12], &ctx);

	/* Return a pointer to where the data should be written. */
	return (&wbuf[16]);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * wire_writepacket_done(W, wbuf, len):
 * Finish writing a packet to the buffered writer ${W}.  The value ${wbuf} must
 * be the pointer returned by wire_writepacket_getbuf(), and the value ${len}
 * must be the value which was passed to wire_writepacket_getbuf().
 */
int
wire_writepacket_done(struct netbuf_write * W, uint8_t * wbuf, size_t len)
{
	CRC32C_CTX ctx;
	uint8_t cbuf[4];
	size_t i;
	uint8_t * header_crc;

	/*
	 * This is safe due to the requirement that ${wbuf} be the pointer
	 * returned by wire_writepacket_getbuf -- that function returns
	 * &wbuf[16], so position [-4] in the new buffer is still within
	 * the allocated memory.
	 */
	header_crc = &wbuf[-4];

	/* Compute the CRC32C of the packet data. */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, wbuf, len);
	CRC32C_Final(cbuf, &ctx);

	/* Write the trailer. */
	for (i = 0; i < 4; i++)
		wbuf[len + i] = cbuf[i] ^ header_crc[i];

	/* We've finished constructing the packet. */
	if (netbuf_write_consume(W, len + 20))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * wire_writepacket(W, packet):
 * Write the packet ${packet} to the buffered writer ${W}.
 */
int
wire_writepacket(struct netbuf_write * W, const struct wire_packet * packet)
{
	uint8_t * wbuf;

	/* Write the packet header. */
	if ((wbuf =
	    wire_writepacket_getbuf(W, packet->ID, packet->len)) == NULL)
		goto err0;

	/* Copy the packet data into place. */
	memcpy(wbuf, packet->buf, packet->len);

	/* Write the packet trailer. */
	if (wire_writepacket_done(W, wbuf, packet->len))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

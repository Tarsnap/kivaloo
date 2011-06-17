#include "mpool.h"

#include "wire.h"

MPOOL(packet, struct wire_packet, 4096);

/**
 * wire_packet_malloc(void):
 * Allocate a wire_packet structure.
 */
struct wire_packet *
wire_packet_malloc(void)
{

	return (mpool_packet_malloc());
}

/**
 * wire_packet_free(P):
 * Free a wire_packet structure (but not its enclosed buffer).
 */
void
wire_packet_free(struct wire_packet * P)
{

	mpool_packet_free(P);
}


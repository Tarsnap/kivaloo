#ifndef _WIRE_H_
#define _WIRE_H_

#include <stddef.h>
#include <stdint.h>

#include "netbuf.h"

struct wire_packet {
	uint64_t ID;
	size_t len;
	uint8_t * buf;
};

/**
 * wire_packet_malloc(void):
 * Allocate a wire_packet structure.
 */
struct wire_packet * wire_packet_malloc(void);

/**
 * wire_packet_free(P):
 * Free a wire_packet structure (but not its enclosed buffer).
 */
void wire_packet_free(struct wire_packet *);

/**
 * wire_readpacket(R, callback, cookie):
 * Read a packet from the buffered reader ${R}.  When a packet has been read,
 * invoke ${callback}(${cookie}, packet); if a failure occurs while reading
 * (e.g., EOF) then invoke the callback with packet == NULL.  The callback is
 * responsible for freeing the provided packet.  Return a cookie which can be
 * passed to wire_readpacket_cancel.
 */
void * wire_readpacket(struct netbuf_read *,
    int (*)(void *, struct wire_packet *), void *);

/**
 * wire_readpacket_cancel(cookie):
 * Cancel the packet read for which ${cookie} was returned.  Do not invoke
 * the packet read callback.
 */
void wire_readpacket_cancel(void *);

/**
 * wire_writepacket(W, packet):
 * Write the packet ${packet} to the buffered writer ${W}.
 */
int wire_writepacket(struct netbuf_write *, const struct wire_packet *);

/**
 * wire_requestqueue_init(s):
 * Create and return a request queue attached to socket ${s}.  The caller is
 * responsibile for ensuring that no attempts are made read/write from/to
 * said socket except via the request queue until wire_requestqueue_destroy
 * is called to destroy the queue.
 */
struct wire_requestqueue * wire_requestqueue_init(int);

/**
 * wire_requestqueue_add(Q, buf, buflen, callback, cookie):
 * Add the ${buflen}-byte request record ${buf} to the request queue ${Q}.
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a reply is received,
 * or with resbuf == NULL if the request failed (because it couldn't be sent
 * or because the connection failed or was destroyed before a response was
 * received).  Note that responses may arrive out-of-order.  The callback is
 * responsible for freeing ${resbuf}.
 */
int wire_requestqueue_add(struct wire_requestqueue *, uint8_t *,
    size_t, int (*)(void *, uint8_t *, size_t), void *);

/**
 * wire_requestqueue_destroy(Q):
 * Destroy the request queue ${Q}.  The response callbacks will be queued to
 * be performed as failures after wire_requestqueue_destroy returns.  On
 * error return, the queue will be destroyed but some callbacks might be lost.
 */
int wire_requestqueue_destroy(struct wire_requestqueue *);

/**
 * wire_requestqueue_free(Q):
 * Free the request queue ${Q}.  The queue must have been previously
 * destroyed by a call to wire_requestqueue_destroy.
 */
void wire_requestqueue_free(struct wire_requestqueue *);

#endif /* !_WIRE_H_ */

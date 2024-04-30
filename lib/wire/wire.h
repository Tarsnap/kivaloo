#ifndef WIRE_H_
#define WIRE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct netbuf_read;
struct netbuf_write;

/* Wire packet data. */
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
 * wire_readpacket_peek(R, P):
 * Look to see if a packet is available from the buffered reader ${R}.  If
 * yes, store it in the packet structure ${P}; otherwise, set ${P}->buf to
 * NULL.  On error (including if a corrupt packet is received) return -1.
 */
int wire_readpacket_peek(struct netbuf_read *, struct wire_packet *);

/**
 * wire_readpacket_wait(R, callback, cookie):
 * Wait until a packet is available to be read from ${R} or a failure occurs
 * while reading (e.g., EOF); then invoke ${callback}(${cookie}, status) where
 * status is 0 on success or 1 on error.  Return a cookie which can be passed
 * to wire_readpacket_wait_cancel().
 */
void * wire_readpacket_wait(struct netbuf_read *, int (*)(void *, int), void *);

/**
 * wire_readpacket_wait_cancel(cookie):
 * Cancel the packet wait for which ${cookie} was returned.  Do not invoke
 * the packet wait callback.
 */
void wire_readpacket_wait_cancel(void *);

/**
 * wire_readpacket_consume(R, P):
 * Consume from the reader ${R} the packet ${P}, which it must have returned
 * via wire_readpacket_peek().
 */
void wire_readpacket_consume(struct netbuf_read *, struct wire_packet *);

/**
 * wire_writepacket_getbuf(W, ID, len):
 * Start writing a packet with ID ${ID} and data length ${len} to the buffered
 * writer ${W}.  Return a pointer to where the data should be written.  This
 * must be followed by a call to wire_writepacket_done().
 */
uint8_t * wire_writepacket_getbuf(struct netbuf_write *, uint64_t, size_t);

/**
 * wire_writepacket_done(W, wbuf, len):
 * Finish writing a packet to the buffered writer ${W}.  The value ${wbuf} must
 * be the pointer returned by wire_writepacket_getbuf(), and the value ${len}
 * must be the value which was passed to wire_writepacket_getbuf().
 */
int wire_writepacket_done(struct netbuf_write *, uint8_t *, size_t);

/**
 * wire_writepacket(W, packet):
 * Write the packet ${packet} to the buffered writer ${W}.
 */
int wire_writepacket(struct netbuf_write *, const struct wire_packet *);

/**
 * wire_requestqueue_init(s):
 * Create and return a request queue attached to socket ${s}.  The caller is
 * responsible for ensuring that no attempts are made read/write from/to
 * said socket except via the request queue until wire_requestqueue_destroy()
 * is called to destroy the queue.
 */
struct wire_requestqueue * wire_requestqueue_init(int);

/**
 * wire_requestqueue_add_getbuf(Q, len, callback, cookie):
 * Start writing a request of length ${len} to the request queue ${Q}.  Return
 * a pointer to where the request packet data should be written.  This must be
 * followed by a call to wire_requestqueue_add_done().
 *
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a response is received,
 * or with resbuf == NULL if the request failed (because it couldn't be sent
 * or because the connection failed or was destroyed before a response was
 * received).  Note that responses may arrive out-of-order.
 */
uint8_t * wire_requestqueue_add_getbuf(struct wire_requestqueue *, size_t,
    int (*)(void *, uint8_t *, size_t), void *);

/**
 * wire_requestqueue_add_done(Q, wbuf, len):
 * Finish writing a request to the request queue ${Q}.  The value ${wbuf} must
 * be the pointer returned by wire_requestqueue_add_getbuf(), and the value ${len}
 * must be the value which was passed to wire_requestqueue_add_getbuf().
 */
int wire_requestqueue_add_done(struct wire_requestqueue *, uint8_t *, size_t);

/**
 * wire_requestqueue_add(Q, buf, buflen, callback, cookie):
 * Add the ${buflen}-byte request record ${buf} to the request queue ${Q}.
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a response is received,
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
 * be performed as failures after wire_requestqueue_destroy() returns.  On
 * error return, the queue will be destroyed but some callbacks might be lost.
 */
int wire_requestqueue_destroy(struct wire_requestqueue *);

/**
 * wire_requestqueue_free(Q):
 * Free the request queue ${Q}.  The queue must have been previously
 * destroyed by a call to wire_requestqueue_destroy().
 */
void wire_requestqueue_free(struct wire_requestqueue *);

#endif /* !WIRE_H_ */

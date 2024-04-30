#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "mpool.h"
#include "netbuf.h"
#include "seqptrmap.h"
#include "warnp.h"

#include "wire.h"

struct request {
	int (* callback)(void *, uint8_t *, size_t);
	void * cookie;
};

struct wire_requestqueue {
	struct netbuf_read * R;
	struct netbuf_write * WQ;
	void * read_cookie;
	struct seqptrmap * reqs;
	int failed;
	int destroyed;
};

MPOOL(request, struct request, 4096);

static int failqueue(void *);

/* Invoke the callback for this request as a failure. */
static int
failreq(void * cookie)
{
	struct request * R = cookie;
	int rc;

	/* Perform a no-response callback. */
	rc = (R->callback)(R->cookie, NULL, 0);

	/* Free the request structure. */
	mpool_request_free(R);

	/* Return status from callback. */
	return (rc);
}

/* Response packet(s) have arrived.  Read and process. */
static int
readpackets(void * cookie, int status)
{
	struct wire_requestqueue * Q = cookie;
	struct wire_packet P;
	struct request * R;

	/* We're not waiting for a packet to be available any more. */
	Q->read_cookie = NULL;

	/* If the wait failed, the connection is dying. */
	if (status)
		goto fail;

	/* Handle packets until there are no more or we encounter an error. */
	do {
		/* Grab a packet. */
		if (wire_readpacket_peek(Q->R, &P))
			goto fail;

		/* Exit the loop if no packet is available. */
		if (P.buf == NULL)
			break;

		/* Look up the request associated with this response. */
		if ((R = seqptrmap_get(Q->reqs, (int64_t)P.ID)) == NULL) {
			/* Is this response ID reasonable? */
			warn0("Received bogus response ID: %016" PRIx64, P.ID);

			/* This connection is not useful. */
			goto fail;
		}

		/* Delete the request from the pending request map. */
		seqptrmap_delete(Q->reqs, (int64_t)P.ID);

		/* Invoke the upstream callback. */
		if ((R->callback)(R->cookie, P.buf, P.len))
			goto err0;

		/* Free the request structure. */
		mpool_request_free(R);

		/* Consume the packet. */
		wire_readpacket_consume(Q->R, &P);
	} while (1);

	/* Wait for another packet to arrive. */
	if ((Q->read_cookie =
	    wire_readpacket_wait(Q->R, readpackets, Q)) == NULL)
		goto err0;

	/* Success! */
	return (0);

fail:
	/* This request queue has failed. */
	return (failqueue(Q));

err0:
	/* Failure! */
	return (-1);
}

/* Kill off this connection, queuing failure callbacks. */
static int
failqueue(void * cookie)
{
	struct wire_requestqueue * Q = cookie;
	struct request * R;
	int64_t ID;
	int rc = 0;	/* Success unless we fail to schedule a callback. */

	/* This queue is dying. */
	assert(Q->failed == 0);
	Q->failed = 1;

	/* Cancel any pending packet-reading wait. */
	if (Q->read_cookie != NULL) {
		wire_readpacket_wait_cancel(Q->read_cookie);
		Q->read_cookie = NULL;
	}

	/* Free the buffered writer. */
	netbuf_write_free(Q->WQ);

	/* Schedule callbacks for pending requests. */
	while ((ID = seqptrmap_getmin(Q->reqs)) != -1) {
		/* Get the first pending request. */
		R = seqptrmap_get(Q->reqs, ID);

		/* Said request can't be NULL unless seqptrmap is broken. */
		assert(R != NULL);

		/* Schedule a failure callback to occur later. */
		if (events_immediate_register(failreq, R, 0) == NULL)
			rc = -1;

		/* Delete the request from the map. */
		seqptrmap_delete(Q->reqs, ID);
	}

	/* Success unless we failed to schedule a callback. */
	return (rc);
}

/**
 * wire_requestqueue_init(s):
 * Create and return a request queue attached to socket ${s}.  The caller is
 * responsible for ensuring that no attempts are made read/write from/to
 * said socket except via the request queue until wire_requestqueue_destroy()
 * is called to destroy the queue.
 */
struct wire_requestqueue *
wire_requestqueue_init(int s)
{
	struct wire_requestqueue * Q;

	/* Allocate structure. */
	if ((Q = malloc(sizeof(struct wire_requestqueue))) == NULL)
		goto err0;

	/* We have not failed, nor have we been destroyed. */
	Q->failed = 0;
	Q->destroyed = 0;

	/* Create a buffered writer. */
	if ((Q->WQ = netbuf_write_init(s, failqueue, Q)) == NULL)
		goto err1;

	/* Create a buffered reader. */
	if ((Q->R = netbuf_read_init(s)) == NULL)
		goto err2;

	/* Create a request ID -> request mapping table. */
	if ((Q->reqs = seqptrmap_init()) == NULL)
		goto err3;

	/* Wait for a packet to arrive. */
	if ((Q->read_cookie =
	    wire_readpacket_wait(Q->R, readpackets, Q)) == NULL)
		goto err4;

	/* Success! */
	return (Q);

err4:
	seqptrmap_free(Q->reqs);
err3:
	netbuf_read_free(Q->R);
err2:
	netbuf_write_free(Q->WQ);
err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * wire_requestqueue_add_getbuf(Q, len, callback, cookie):
 * Start writing a request of length ${len} to the request queue ${Q}.  Return
 * a pointer to where the request packet data should be written.  This must be
 * followed by a call to wire_requestqueue_add_done().
 *
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a reply is received,
 * or with resbuf == NULL if the request failed (because it couldn't be sent
 * or because the connection failed or was destroyed before a response was
 * received).  Note that responses may arrive out-of-order.
 */
uint8_t *
wire_requestqueue_add_getbuf(struct wire_requestqueue * Q, size_t len,
    int (* callback)(void *, uint8_t *, size_t), void * cookie)
{
	struct request * R;
	uint8_t * wbuf;
	uint64_t ID;

	/* Bake a cookie. */
	if ((R = mpool_request_malloc()) == NULL)
		goto err0;
	R->callback = callback;
	R->cookie = cookie;

	/* If the request queue has failed, we can't send a request. */
	if (Q->failed) {
		/* Schedule a failure callback. */
		if (events_immediate_register(failreq, R, 0) == NULL)
			goto err1;

		/* Return a dummy buffer for the request. */
		return (malloc(len));
	}

	/* Insert the cookie into the pending request map. */
	if ((ID = (uint64_t)seqptrmap_add(Q->reqs, R)) == (uint64_t)(-1))
		goto err1;

	/* Start writing a packet. */
	if ((wbuf = wire_writepacket_getbuf(Q->WQ, ID, len)) == NULL)
		goto err2;

	/* Return a pointer to the packet data buffer. */
	return (wbuf);

err2:
	seqptrmap_delete(Q->reqs, (int64_t)ID);
err1:
	mpool_request_free(R);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * wire_requestqueue_add_done(Q, wbuf, len):
 * Finish writing a request to the request queue ${Q}.  The value ${wbuf} must
 * be the pointer returned by wire_requestqueue_add_getbuf(), and the value ${len}
 * must be the value which was passed to wire_requestqueue_add_getbuf().
 */
int
wire_requestqueue_add_done(struct wire_requestqueue * Q, uint8_t * wbuf,
    size_t len)
{

	/* If the request queue has failed, just free the dummy buffer. */
	if (Q->failed) {
		free(wbuf);
		return (0);
	}

	/* We've finished writing this packet. */
	return (wire_writepacket_done(Q->WQ, wbuf, len));
}

/**
 * wire_requestqueue_add(Q, buf, buflen, callback, cookie):
 * Add the ${buflen}-byte request record ${buf} to the request queue ${Q}.
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a reply is received,
 * or with resbuf == NULL if the request failed (because it couldn't be sent
 * or because the connection failed or was destroyed before a response was
 * received).  Note that responses may arrive out-of-order.  The callback is
 * responsible for freeing ${resbuf}.
 */
int
wire_requestqueue_add(struct wire_requestqueue * Q,
    uint8_t * buf, size_t buflen,
    int (* callback)(void *, uint8_t *, size_t), void * cookie)
{
	uint8_t * wbuf;

	/* Start writing the request. */
	if ((wbuf = wire_requestqueue_add_getbuf(Q, buflen,
	    callback, cookie)) == NULL)
		goto err0;

	/* Copy the request data into the provided buffer. */
	memcpy(wbuf, buf, buflen);

	/* Finish writing the request. */
	if (wire_requestqueue_add_done(Q, wbuf, buflen))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * wire_requestqueue_destroy(Q):
 * Destroy the request queue ${Q}.  The response callbacks will be queued to
 * be performed as failures after wire_requestqueue_destroy() returns.  On
 * error return, the queue will be destroyed but some callbacks might be lost.
 */
int
wire_requestqueue_destroy(struct wire_requestqueue * Q)
{

	/* We are being destroyed. */
	Q->destroyed = 1;

	/* If we've already failed, there's no need to do anything more. */
	if (Q->failed)
		return (0);

	/* Tear everything down as if there was an error. */
	return (failqueue(Q));
}

/**
 * wire_requestqueue_free(Q):
 * Free the request queue ${Q}.  The queue must have been previously
 * destroyed by a call to wire_requestqueue_destroy().
 */
void
wire_requestqueue_free(struct wire_requestqueue * Q)
{

	/* Sanity check: The queue must have been destroyed. */
	assert(Q->destroyed);

	/* Sanity check: There must be no pending requests. */
	assert(seqptrmap_getmin(Q->reqs) == -1);

	/* Free the pending request map. */
	seqptrmap_free(Q->reqs);

	/* Free the buffered reader. */
	netbuf_read_free(Q->R);

	/* Free the request queue. */
	free(Q);
}

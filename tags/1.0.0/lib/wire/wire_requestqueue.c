#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "mpool.h"
#include "netbuf.h"
#include "seqptrmap.h"
#include "warnp.h"

#include "wire.h"

struct request {
	struct wire_requestqueue * Q;
	int (* callback)(void *, uint8_t *, size_t);
	void * cookie;
	struct wire_packet * req;
	uint8_t * resbuf;
	size_t resbuflen;
	int cbpending;
};

struct wire_requestqueue {
	struct netbuf_read * R;
	struct netbuf_write * WQ;
	void * read_cookie;
	struct seqptrmap * reqs;
	int destroyed;
};

MPOOL(request, struct request, 4096);

static int failqueue(struct wire_requestqueue *);

/* Request writ or response received; callback and cleanup if appropriate. */
static int
cbdone(void * cookie)
{
	struct request * R = cookie;
	int rc;

	/* One of the callbacks has occurred. */
	R->cbpending--;

	/* If we're still waiting for another callback, stop here. */
	if (R->cbpending)
		return (0);

	/* Perform the callback. */
	rc = (R->callback)(R->cookie, R->resbuf, R->resbuflen);

	/*
	 * Free the request packet structure (but not the record buffer,
	 * which is freed upstream) and the request structure.
	 */
	wire_packet_free(R->req);
	mpool_request_free(R);

	/* Return status from callback. */
	return (rc);
}

/* A packet was written (or not). */
static int
writpacket(void * cookie, int status)
{
	struct request * R = cookie;
	struct wire_requestqueue * Q = R->Q;
	int rc = 0;

	/* Did we fail? */
	if (status) {
		if (failqueue(Q))
			rc = -1;
	}

	/* One of the callbacks for this request has been performed. */
	if (cbdone(R))
		rc = -1;

	return (rc);
}

/* Handle an incoming response. */
static int
gotpacket(void * cookie, struct wire_packet * P)
{
	struct wire_requestqueue * Q = cookie;
	struct request * R;
	int rc;

	/* This packet read is no longer in progress. */
	Q->read_cookie = NULL;

	/* If the packet read failed, the connection is dying. */
	if (P == NULL) {
		goto fail;
	}

	/* Look up the request associated with this response. */
	if ((R = seqptrmap_get(Q->reqs, P->ID)) == NULL) {
		/* Server sent a response to a request we didn't send? */
		warn0("Received bogus response ID: %016" PRIx64, P->ID);

		/* This connection is not useful. */
		goto fail1;
	}

	/* Delete the request from the pending request map. */
	seqptrmap_delete(Q->reqs, P->ID);

	/* Keep the response length and buffer; free the packet structure. */
	R->resbuf = P->buf;
	R->resbuflen = P->len;
	wire_packet_free(P);

	/* Perform the upstream callback if appropriate. */
	rc = cbdone(R);

	/* Start reading another packet. */
	if ((Q->read_cookie = wire_readpacket(Q->R, gotpacket, Q)) == NULL)
		goto err0;

	/* Success! */
	return (rc);

fail1:
	/* Free the packet. */
	free(P->buf);
	wire_packet_free(P);
fail:
	/* This request queue has failed. */
	return (failqueue(Q));

err0:
	/* Failure! */
	return (-1);
}

/* Kill off this connection, causing all pending callbacks to be invoked. */
static int
failqueue(struct wire_requestqueue * Q)
{
	int64_t ID;
	struct request * R;

	/* Cancel any pending read. */
	if (Q->read_cookie != NULL) {
		wire_readpacket_cancel(Q->read_cookie);
		Q->read_cookie = NULL;
	}

	/* Schedule callbacks for pending requests. */
	while ((ID = seqptrmap_getmin(Q->reqs)) != -1) {
		/* Get the first pending request. */
		R = seqptrmap_get(Q->reqs, ID);

		/* Said request can't be NULL unless seqptrmap is broken. */
		assert(R != NULL);

		/*
		 * Schedule a "callback done" for this request and delete it
		 * from the map; we can't do these in line since they might
		 * end up doing callbacks which invoke _destroy and end up
		 * back here.  Since we haven't read a response for this
		 * request yet (if we had, it wouldn't be in the map), the
		 * upstream callback will be invoked as a failure.
		 */
		if (!events_immediate_register(cbdone, R, 0))
			goto err0;
		seqptrmap_delete(Q->reqs, ID);
	}

	/*
	 * Destroy (but do not free) the write queue.  All new packet writes
	 * hereafter will immediately fail, bringing us back to here.
	 */
	if (netbuf_write_destroy(Q->WQ))
		goto err0;

	/* Succcess! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * wire_requestqueue_init(s):
 * Create and return a request queue attached to socket ${s}.  The caller is
 * responsibile for ensuring that no attempts are made read/write from/to
 * said socket except via the request queue until wire_requestqueue_destroy
 * is called to destroy the queue.
 */
struct wire_requestqueue *
wire_requestqueue_init(int s)
{
	struct wire_requestqueue * Q;

	/* Allocate structure. */
	if ((Q = malloc(sizeof(struct wire_requestqueue))) == NULL)
		goto err0;

	/* We have not called wire_requestqueue_destroy yet. */
	Q->destroyed = 0;

	/* Create a buffered writer. */
	if ((Q->WQ = netbuf_write_init(s)) == NULL)
		goto err1;

	/* Create a buffered reader. */
	if ((Q->R = netbuf_read_init(s)) == NULL)
		goto err2;

	/* Create a request ID -> request mapping table. */
	if ((Q->reqs = seqptrmap_init()) == NULL)
		goto err3;

	/* Start reading packets. */
	if ((Q->read_cookie = wire_readpacket(Q->R, gotpacket, Q)) == NULL)
		goto err4;

	/* Success! */
	return (Q);

err4:
	seqptrmap_free(Q->reqs);
err3:
	netbuf_read_free(Q->R);
err2:
	netbuf_write_destroy(Q->WQ);
	netbuf_write_free(Q->WQ);
err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * wire_requestqueue_add(Q, buf, buflen, callback, cookie):
 * Add the ${buflen}-byte request record ${buf} to the request queue ${Q}.
 * Invoke ${callback}(${cookie}, resbuf, resbuflen) when a reply is received,
 * or with resbuf == NULL if the request failed (because it couldn't be sent
 * or because the connection failed or was destroyed before a response was
 * received).  Note that responses may arrive out-of-order.  The buffer
 * ${buf} must remain valid until the callback is invoked.  The callback is
 * responsible for freeing ${resbuf}.
 */
int
wire_requestqueue_add(struct wire_requestqueue * Q,
    uint8_t * buf, size_t buflen,
    int (* callback)(void *, uint8_t *, size_t), void * cookie)
{
	struct request * R;

	/* Bake a cookie. */
	if ((R = mpool_request_malloc()) == NULL)
		goto err0;
	R->Q = Q;
	R->callback = callback;
	R->cookie = cookie;
	R->cbpending = 2;
	R->resbuf = NULL;
	R->resbuflen = 0;

	/* Create a packet. */
	if ((R->req = wire_packet_malloc()) == NULL)
		goto err1;
	R->req->buf = buf;
	R->req->len = buflen;

	/* Insert the cookie into the pending request map. */
	if ((R->req->ID = seqptrmap_add(Q->reqs, R)) == (uint64_t)(-1))
		goto err2;

	/* Queue the request packet to be written. */
	if (wire_writepacket(Q->WQ, R->req, writpacket, R))
		goto err3;

	/* Success! */
	return (0);

err3:
	seqptrmap_delete(Q->reqs, R->req->ID);
err2:
	wire_packet_free(R->req);
err1:
	mpool_request_free(R);
err0:
	/* Failure! */
	return (-1);
}

/**
 * wire_requestqueue_destroy(Q):
 * Destroy the request queue ${Q}.  The response callbacks will be queued to
 * be performed as failures after wire_requestqueue_destroy returns.  On
 * error return, the queue will be destroyed but some callbacks might be lost.
 */
int
wire_requestqueue_destroy(struct wire_requestqueue * Q)
{

	/* Record that wire_requestqueue_destroy has been called. */
	Q->destroyed = 1;

	/* Tear everything down as if there was an error. */
	return (failqueue(Q));
}

/**
 * wire_requestqueue_free(Q):
 * Free the request queue ${Q}.  The queue must have been previously
 * destroyed by a call to wire_requestqueue_destroy and there must be no
 * pending requests.
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

	/* Free the buffered writer. */
	netbuf_write_free(Q->WQ);

	/* Free the request queue. */
	free(Q);	
}

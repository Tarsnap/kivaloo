#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

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
	if (P == NULL)
		goto fail;

	/* Look up the request associated with this response. */
	if ((R = seqptrmap_get(Q->reqs, P->ID)) == NULL) {
		/* Server sent a response to a request we didn't send? */
		warn0("Received bogus response ID: %016" PRIx64, P->ID);

		/* This connection is not useful. */
		goto fail1;
	}

	/* Delete the request from the pending request map. */
	seqptrmap_delete(Q->reqs, P->ID);

	/* Invoke the upstream callback. */
	rc = (R->callback)(R->cookie, P->buf, P->len);

	/*
	 * Free the response packet structure (but not the buffer, which is
	 * the responsibility of the response-handling callback code..
	 */
	wire_packet_free(P);

	/* Free the request structure. */
	mpool_request_free(R);

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

	/* Cancel any pending read. */
	if (Q->read_cookie != NULL) {
		wire_readpacket_cancel(Q->read_cookie);
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
 * received).  Note that responses may arrive out-of-order.  The callback is
 * responsible for freeing ${resbuf}.
 */
int
wire_requestqueue_add(struct wire_requestqueue * Q,
    uint8_t * buf, size_t buflen,
    int (* callback)(void *, uint8_t *, size_t), void * cookie)
{
	struct request * R;
	struct wire_packet req;

	/* Bake a cookie. */
	if ((R = mpool_request_malloc()) == NULL)
		goto err0;
	R->callback = callback;
	R->cookie = cookie;

	/* If the request queue has failed, just schedule a callback. */
	if (Q->failed) {
		if (events_immediate_register(failreq, R, 0) == NULL)
			goto err1;
		else
			goto done;
	}

	/* Insert the cookie into the pending request map. */
	if ((req.ID = seqptrmap_add(Q->reqs, R)) == (uint64_t)(-1))
		goto err1;

	/* Fill in packet structure fields. */
	req.buf = buf;
	req.len = buflen;

	/* Queue the request packet to be written. */
	if (wire_writepacket(Q->WQ, &req))
		goto err2;

done:
	/* Success! */
	return (0);

err2:
	seqptrmap_delete(Q->reqs, req.ID);
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
 * destroyed by a call to wire_requestqueue_destroy.
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

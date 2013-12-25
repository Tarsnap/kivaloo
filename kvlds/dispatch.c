#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include "events.h"
#include "imalloc.h"
#include "kvldskey.h"
#include "mpool.h"
#include "netbuf.h"
#include "network.h"
#include "proto_kvlds.h"
#include "serialize.h"
#include "warnp.h"

#include "btree.h"
#include "btree_cleaning.h"
#include "node.h"

#include "dispatch.h"

/* Maximum number of requests to have pending at once. */
#define MAXREQS	4096

/* Linked list of requests. */
struct requestq {
	/* The request. */
	struct proto_kvlds_request * R;

	/* Next request in the linked list. */
	struct requestq * next;

	/* Used for NMRs after dequeueing. */
	struct dispatch_state * D;
	size_t npages;
};

/* Request dispatcher state. */
struct dispatch_state {
	/* Connection management. */
	int dying;			/* Our connection is dying. */
	int s;				/* Connected socket. */
	struct netbuf_read * readq;	/* Packet read queue. */
	struct netbuf_write * writeq;	/* Packet write queue. */
	void * read_cookie;		/* Request read cookie. */
	size_t nrequests;		/* Number of responses we owe. */

	/* Operational parameters. */
	struct btree * T;		/* The B+Tree we're working on. */
	size_t kmax;			/* Maximum permitted key length. */
	size_t vmax;			/* Maximum permitted value length. */

	/* Non-modifying requests. */
	struct requestq * nmr_head;	/* First request in the queue. */
	struct requestq ** nmr_tail;	/* Pointer to final NULL. */
	size_t nmr_ip;			/* Pages touched by ongoing NMRs. */
	size_t nmr_concurrency;		/* Max # pages touched by NMRs. */

	/* Modifying requests. */
	struct requestq * mr_head;	/* First request in the queue. */
	struct requestq ** mr_tail;	/* Pointer to final NULL. */
	size_t mr_reqs;			/* # requests in current batch. */
	size_t mr_concurrency;		/* Max # pages touched by MRs. */

	/* Stop-queuing-MRs-yet-and-start-processing-them controls. */
	int mr_inprogress;		/* Nonzero if MRs are in progress. */
	size_t mr_qlen;			/* Number of queued MRs. */
	void * mr_timer;		/* Cookie from events_timer. */
	int mr_timer_expired;		/* Timer has expired. */
	struct timeval mr_timeout;	/* Maximum time for MR to wait. */
	size_t mr_min_batch;		/* Minimum MR batch w/o timeout. */

	/* Cleaning-flush timer. */
	void * mrc_timer;		/* Cookie from events_timer. */
	int docleans;			/* Cleaning needs a batch of MRs. */
};

MPOOL(requestq, struct requestq, 4096);

static int callback_accept(void *, int);
static int dropconnection(void *);
static int poke_nmr(struct dispatch_state *);
static int callback_nmr_done(void *);
static int poke_mr(struct dispatch_state *);
static int callback_mr_timer(void *);
static int callback_mrc_timer(void *);
static int callback_mr_done(void *);
static int gotrequest(void *, int);
static int readreqs(struct dispatch_state *);

/* Time between ticks of the 'flush cleans if we have had no MRs' clock. */
static const struct timeval fivesec = {.tv_sec = 5, .tv_usec = 0};

/* The connection is dying.  Help speed up the process. */
static int
dropconnection(void * cookie)
{
	struct dispatch_state * D = cookie;
	struct requestq * RQ;

	/* This connection is dying. */
	D->dying = 1;

	/* If we're reading a packet, stop it. */
	if (D->read_cookie != NULL) {
		wire_readpacket_wait_cancel(D->read_cookie);
		D->read_cookie = NULL;
	}

	/* Free queued requests. */
	while ((RQ = D->nmr_head) != NULL) {
		/* Remove from the queue. */
		D->nmr_head = RQ->next;

		/* Free the request and linked list node. */
		proto_kvlds_request_free(RQ->R);
		mpool_requestq_free(RQ);

		/* That's one request we won't be responding to. */
		D->nrequests -= 1;
	}
	while ((RQ = D->mr_head) != NULL) {
		/* Remove from the queue. */
		D->mr_head = RQ->next;
		D->mr_qlen -= 1;

		/* Free the request and linked list node. */
		proto_kvlds_request_free(RQ->R);
		mpool_requestq_free(RQ);

		/* That's one request we won't be responding to. */
		D->nrequests -= 1;
	}

	/* Cancel any stop-queuing timer; we've freed the queue anyway. */
	if (D->mr_timer != NULL) {
		events_timer_cancel(D->mr_timer);
		D->mr_timer = NULL;
	}

	/* The (unset) timer hasn't expired. */
	D->mr_timer_expired = 0;

	/* Success!  (We can't fail -- but netbuf_write doesn't know that.) */
	return (0);
}

/* Launch non-modifying requests, if possible. */
static int
poke_nmr(struct dispatch_state * D)
{
	struct requestq * RQ;

	/* If we have any queued requests, try to launch them. */
	while ((RQ = D->nmr_head) != NULL) {
		/* How many pages would this request need to touch? */
		if (RQ->R->type == PROTO_KVLDS_GET)
			RQ->npages = D->T->root_shadow->height + 1;
		else
			RQ->npages = D->T->root_shadow->height +
			    D->T->pagelen / SERIALIZE_PERCHILD;

		/* Can we handle this request? */
		if ((D->nmr_ip > 0) &&
		    (D->nmr_ip + RQ->npages > D->nmr_concurrency))
			break;

		/* Dequeue this request. */
		D->nmr_head = RQ->next;

		/* Launch the request. */
		RQ->D = D;
		if (dispatch_nmr_launch(D->T, RQ->R, D->writeq,
		    callback_nmr_done, RQ))
			goto err0;
		D->nmr_ip += RQ->npages;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* An NMR has been completed. */
static int
callback_nmr_done(void * cookie)
{
	struct requestq * RQ = cookie;
	struct dispatch_state * D = RQ->D;

	/* This NMR is no longer in progress. */
	D->nmr_ip -= RQ->npages;

	/* Free request cookie. */
	mpool_requestq_free(RQ);

	/* We've finished with this request. */
	D->nrequests -= 1;

	/* Check if we need to read more requests. */
	if (readreqs(D))
		goto err0;

	/* Poke the queue in case we can now handle another request. */
	if (poke_nmr(D))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Launch modifying requests or start a timer if necessary. */
static int
poke_mr(struct dispatch_state * D)
{
	size_t concurrency = D->mr_concurrency;
	size_t pagesperop = D->T->root_dirty->height + 1;
	struct proto_kvlds_request ** reqs;
	struct requestq * RQ;
	size_t i;

	/* Launch a batch of requests if possible. */
	if ((D->mr_inprogress == 0) &&
	    ((D->mr_timer_expired != 0) ||
	     (D->docleans != 0) ||
	     (D->mr_qlen >= D->mr_min_batch))) {
		/* Figure out how many requests will be in this batch. */
		if (D->mr_qlen * pagesperop > concurrency)
			D->mr_reqs = concurrency / pagesperop;
		else
			D->mr_reqs = D->mr_qlen;

		/* Allocate an array. */
		if (IMALLOC(reqs, D->mr_reqs, struct proto_kvlds_request *))
			goto err0;

		/* Fill the array with requests. */
		for (i = 0; i < D->mr_reqs; i++) {
			/* We should have a request. */
			assert(D->mr_head != NULL);

			/* Dequeue. */
			RQ = D->mr_head;
			D->mr_head = RQ->next;
			D->mr_qlen -= 1;

			/* Insert into the array. */
			reqs[i] = RQ->R;

			/* Free linked list node. */
			mpool_requestq_free(RQ);
		}

		/* Modifying requests are now in progress. */
		D->mr_inprogress = 1;

		/* Launch the batch of modifying requests. */
		if (dispatch_mr_launch(D->T, reqs, D->mr_reqs, D->writeq,
		    callback_mr_done, D))
			goto err1;

		/* We beat the clock.  Disable it. */
		if (D->mr_timer != NULL) {
			events_timer_cancel(D->mr_timer);
			D->mr_timer = NULL;
		}

		/* The (unset) timer hasn't expired. */
		D->mr_timer_expired = 0;

		/* Reset the do-a-cleaning-only-batch timer to 5 seconds. */
		if (D->mrc_timer != NULL) {
			events_timer_cancel(D->mrc_timer);
			D->mrc_timer = NULL;
		}
		if ((D->mrc_timer = events_timer_register(callback_mrc_timer,
		    D, &fivesec)) == NULL)
			goto err0;

		/* We don't need to launch another batch any more. */
		D->docleans = 0;
	}

	/*
	 * If we have requests and the clock isn't ticking (and hasn't
	 * already expired), then start the timer.  Note that this is useful
	 * even with a mr_timeout value of zero, since network events take
	 * priority over timers -- if multiple requests arrive simultaneously
	 * this allows them to all be included in the batch.
	 */
	if ((D->mr_timer == NULL) && (D->mr_timer_expired == 0) &&
	    (D->mr_qlen > 0)) {
		if ((D->mr_timer = events_timer_register(callback_mr_timer,
		    D, &D->mr_timeout)) == NULL)
			goto err0;
	}

	/* Success! */
	return (0);

err1:
	/* These requests can never be done, but at least we can free them. */
	for (i = 0; i < D->mr_reqs; i++)
		proto_kvlds_request_free(reqs[i]);
	free(reqs);
err0:
	/* Failure! */
	return (-1);
}

/* The MR timer has expired. */
static int
callback_mr_timer(void * cookie)
{
	struct dispatch_state * D = cookie;

	/* The timer has expired. */
	D->mr_timer_expired = 1;

	/* We don't have a timer any more. */
	D->mr_timer = NULL;

	/* Launch modifying requests if possible. */
	return (poke_mr(D));
}

/* The cleaning timer has expired. */
static int
callback_mrc_timer(void * cookie)
{
	struct dispatch_state * D = cookie;

	/* If we have no pending cleaning, reset the timer. */
	if (!btree_cleaning_possible(D->T->cstate)) {
		if ((D->mrc_timer = events_timer_register(callback_mrc_timer,
		    D, &fivesec)) == NULL)
			return (-1);
		else
			return (0);
	}

	/* This timer isn't ticking any more. */
	D->mrc_timer = NULL;

	/* We want to force a batch of MRs so that we can clean. */
	D->docleans = 1;

	/* Launch a (possibly empty) batch of MRs if possible. */
	return (poke_mr(D));
}

/* A batch of MRs has been completed. */
static int
callback_mr_done(void * cookie)
{
	struct dispatch_state * D = cookie;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(D->T);
#endif

	/* We've handled a bunch of requests. */
	D->nrequests -= D->mr_reqs;

	/* No MRs are in progress any more. */
	D->mr_inprogress = 0;

	/* Check if we need to read more requests. */
	if (readreqs(D))
		goto err0;

	/* Maybe we can launch some more MRs? */
	return (poke_mr(D));

err0:
	/* Failure! */
	return (-1);
}

/* Start reading a request if it is appropriate to do so. */
static int
readreqs(struct dispatch_state * D)
{

	/* If this connection is dying, do nothing. */
	if (D->dying)
		goto done;

	/* If we don't have a reader, don't try to read. */
	if (D->readq == NULL)
		goto done;

	/* If we are already reading, do nothing. */
	if (D->read_cookie != NULL)
		goto done;

	/* If we have MAXREQS requests in progress, do nothing. */
	if (D->nrequests == MAXREQS)
		goto done;

	/* Wait for a request to arrive. */
	if ((D->read_cookie = wire_readpacket_wait(D->readq,
	    gotrequest, D)) == NULL) {
		warnp("Error reading request from connection");
		goto err0;
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Read and dispatch incoming request(s). */
static int
gotrequest(void * cookie, int status)
{
	struct dispatch_state * D = cookie;
	struct proto_kvlds_request * R;
	struct requestq * RQ;

	/* We're no longer waiting for a packet to arrive. */
	D->read_cookie = NULL;

	/* If the wait failed, the connection is dead. */
	if (status)
		goto drop;

	/*
	 * Read packets until there are no more to read, we hit MAXREQS, or
	 * an error occurs.
	 */
	do {
		/* Allocate space for a request. */
		if ((R = proto_kvlds_request_alloc()) == NULL)
			goto err0;

		/* If we have MAXREQS requests, stop looping. */
		if (D->nrequests == MAXREQS)
			break;

		/* Attempt to read a request. */
		if (proto_kvlds_request_read(D->readq, R))
			goto drop1;

		/* If we have no request, stop looping. */
		if (R->type == PROTO_KVLDS_NONE)
			break;

		/* We owe a response to the client. */
		D->nrequests += 1;

		/* Construct a linked list node. */
		if ((RQ = mpool_requestq_malloc()) == NULL)
			goto err1;
		RQ->R = R;
		RQ->next = NULL;

		/* Add to the modifying or non-modifying queue, and poke it. */
		switch (R->type) {
		case PROTO_KVLDS_PARAMS:
			/* Send the response immediately. */
			if (proto_kvlds_response_params(D->writeq, RQ->R->ID,
			D->kmax, D->vmax))
				goto err2;

			/* Free the linked list node. */
			mpool_requestq_free(RQ);

			/* Free the request packet. */
			proto_kvlds_request_free(R);

			/* This request has been handled. */
			D->nrequests -= 1;
			break;
		case PROTO_KVLDS_CAS:
		case PROTO_KVLDS_SET:
		case PROTO_KVLDS_ADD:
		case PROTO_KVLDS_MODIFY:
			/*
			* We can't add or modify a key-value pair if the key is too
			* long or the value we're setting is too long.
			*/
			if ((R->key->len > D->kmax) ||
			(R->value->len > D->vmax))
				goto drop2;

			/* FALLTHROUGH */

		case PROTO_KVLDS_DELETE:
		case PROTO_KVLDS_CAD:
			/* Add to modifying request queue. */
			if (D->mr_head == NULL)
				D->mr_head = RQ;
			else
				*(D->mr_tail) = RQ;
			D->mr_tail = &RQ->next;

			/* The MR queue has gained an element. */
			D->mr_qlen += 1;

			/* Poke the queue. */
			if (poke_mr(D))
				goto err0;
			break;
		case PROTO_KVLDS_GET:
		case PROTO_KVLDS_RANGE:
			/* Add to non-modifying request queue. */
			if (D->nmr_head == NULL)
				D->nmr_head = RQ;
			else
				*(D->nmr_tail) = RQ;
			D->nmr_tail = &RQ->next;

			/* Poke the queue. */
			if (poke_nmr(D))
				goto err0;
			break;
		default:
			/* Don't recognize this packet... */
			warn0("Received unrecognized packet type: 0x%08" PRIx32,
			R->type);
			goto drop2;
		}
	} while (1);

	/* Free the (unused) request structure. */
	proto_kvlds_request_free(R);

	/* Wait for more requests to arrive. */
	if (readreqs(D))
		goto err0;

	/* Success! */
	return (0);

drop2:
	mpool_requestq_free(RQ);
	D->nrequests -= 1;
drop1:
	proto_kvlds_request_free(R);
drop:
	/* We didn't get a valid request.  Drop the connection. */
	dropconnection(D);

	/* All is good. */
	return (0);

err2:
	mpool_requestq_free(RQ);
err1:
	D->nrequests -= 1;
	proto_kvlds_request_free(R);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_accept(s, T, kmax, vmax, w, g):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for the B+Tree ${T}.  Keys will be at most ${kmax} bytes; values
 * will be at most ${vmax} bytes; up to ${w} seconds should be spent waiting
 * for more requests before performing a group commit, unless ${g} requests
 * are pending.
 */
struct dispatch_state *
dispatch_accept(int s, struct btree * T,
    size_t kmax, size_t vmax, double w, size_t g)
{
	struct dispatch_state * D;

	/* Allocate space for dispatcher state. */
	if ((D = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;

	/* Initialize dispatcher. */
	D->dying = 0;
	D->readq = NULL;
	D->read_cookie = NULL;
	D->T = T;
	D->kmax = kmax;
	D->vmax = vmax;
	D->nrequests = 0;
	D->nmr_head = NULL;
	D->nmr_ip = 0;
	D->nmr_concurrency = T->poolsz / 4;
	D->mr_head = NULL;
	D->mr_reqs = 0;
	D->mr_concurrency = T->poolsz / 4;
	D->mr_inprogress = 0;
	D->mr_qlen = 0;
	D->mr_timer = NULL;
	D->mr_timer_expired = 0;
	D->mr_timeout.tv_sec = w;
	D->mr_timeout.tv_usec = (w - D->mr_timeout.tv_sec) * 1000000;
	D->mr_min_batch = g;

	/* Start the periodic cleaning timer. */
	D->docleans = 0;
	if ((D->mrc_timer = events_timer_register(callback_mrc_timer, D,
	    &fivesec)) == NULL) {
		warnp("events_timer_register");
		goto err1;
	}

	/* Accept a connection. */
	if (network_accept(s, callback_accept, D) == NULL)
		goto err2;

	/* Success! */
	return (D);

err2:
	events_timer_cancel(D->mrc_timer);
err1:
	free(D);
err0:
	/* Failure! */
	return (NULL);
}

/* A connection has arrived. */
static int
callback_accept(void * cookie, int s)
{
	struct dispatch_state * D = cookie;

	/* We have a socket. */
	if ((D->s = s) == -1) {
		warnp("Error accepting connection");
		goto err0;
	}

	/* Make the accepted connection non-blocking. */
	if (fcntl(D->s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make connection non-blocking");
		goto err1;
	}

	/* Create a buffered writer for the connection. */
	if ((D->writeq = netbuf_write_init(D->s, dropconnection, D)) == NULL) {
		warnp("Cannot create packet write queue");
		goto err1;
	}

	/* Create a buffered reader for the connection. */
	if ((D->readq = netbuf_read_init(D->s)) == NULL) {
		warn0("Cannot create packet read queue");
		goto err2;
	}

	/* Start listening for packets. */
	if (readreqs(D))
		goto err3;

	/* Success! */
	return (0);

err3:
	netbuf_read_free(D->readq);
err2:
	netbuf_write_free(D->writeq);
err1:
	close(D->s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_alive(D):
 * Return non-zero iff the dispatch state ${D} is still alive (if it is
 * waiting for a connection to arrive, is reading requests, or has requests
 * in progress).
 */
int
dispatch_alive(struct dispatch_state * D)
{

	return ((D->dying == 0) || (D->nrequests > 0));
}

/**
 * dispatch_done(D):
 * Clean up the dispatch state ${D}.  The function dispatch_alive(${D}) must
 * have previously returned zero.
 */
int
dispatch_done(struct dispatch_state * D)
{

	/*
	 * There should not be a MR timer running, because there should be
	 * no requests in progress.  We should not be reading a request, and
	 * the connection should be dying.
	 */
	assert(D->mr_timer == NULL);
	assert(D->nrequests == 0);
	assert(D->read_cookie == NULL);
	assert(D->dying == 1);

	/* Stop the cleaning timer. */
	events_timer_cancel(D->mrc_timer);

	/* Free the buffered reader. */
	netbuf_read_free(D->readq);

	/* Free the buffered writer. */
	netbuf_write_free(D->writeq);

	/* Close the socket. */
	while (close(D->s)) {
		if (errno == EINTR)
			continue;
		warnp("close");
		goto err1;
	}

	/* Free the dispatcher state. */
	free(D);

	/* Success! */
	return (0);

err1:
	free(D);

	/* Failure! */
	return (-1);
}

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "monoclock.h"
#include "mpool.h"
#include "netbuf.h"
#include "network.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "perfstats.h"

#include "dispatch.h"

/* Request dispatcher state. */
struct dispatch_state {
	/* Connection management. */
	int s;				/* Source socket. */
	struct netbuf_read * readq;	/* Source read queue. */
	struct netbuf_write * writeq;	/* Source write queue. */
	void * accept_cookie;		/* Source connection accept cookie. */
	void * read_cookie;		/* Source packet read cookie. */
	struct wire_requestqueue * Q;	/* Connected to target. */
	size_t nrequests;		/* # requests in flight. */
	struct perfstats * P;		/* Statistics gathering state. */
};

/* In-flight request state. */
struct forwardee {
	struct dispatch_state * D;	/* Dispatcher. */
	uint64_t ID;			/* Request ID. */
	struct timeval t_start;		/* Start time. */
	uint32_t reqtype;		/* Request type. */
};

MPOOL(forwardee, struct forwardee, 32768);

static int callback_accept(void *, int);
static int callback_gotrequests(void *, int);
static int callback_gotresponse(void *, uint8_t *, size_t);

/**
 * dispatch_accept(s, Q, P):
 * Initialize a dispatcher to accept a connection from the listening socket
 * ${s} and shuttle request/responses to/from the request queue ${Q}, recording
 * performance for each request via ${P}.
 */
struct dispatch_state *
dispatch_accept(int s, struct wire_requestqueue * Q, struct perfstats * P)
{
	struct dispatch_state * D;

	/* Bake a cookie. */
	if ((D = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;

	/* Initialize dispatcher. */
	D->readq = NULL;
	D->writeq = NULL;
	D->read_cookie = NULL;
	D->Q = Q;
	D->nrequests = 0;
	D->P = P;

	/* Accept a connection. */
	if ((D->accept_cookie = network_accept(s, callback_accept, D)) == NULL)
		goto err1;

	/* Success! */
	return (D);

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

	/* We are no longer waiting for a connection to arrive. */
	D->accept_cookie = NULL;

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
	if ((D->writeq = netbuf_write_init(D->s, NULL, NULL)) == NULL) {
		warnp("Cannot create packet write queue");
		goto err1;
	}

	/* Create a buffered reader for the connection. */
	if ((D->readq = netbuf_read_init(D->s)) == NULL) {
		warn0("Cannot create packet read queue");
		goto err2;
	}

	/* Wait for a request to arrive. */
	if ((D->read_cookie = wire_readpacket_wait(D->readq,
	    callback_gotrequests, D)) == NULL) {
		warnp("Error reading request from connection");
		goto err3;
	}

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

/* Packet(s) have arrived from the source socket. */
static int
callback_gotrequests(void * cookie, int status)
{
	struct dispatch_state * D = cookie;
	struct wire_packet P;
	struct forwardee * F;

	/* We're not waiting for a packet to be available any more. */
	D->read_cookie = NULL;

	/* If the wait failed, the connection is dying. */
	if (status)
		goto fail;

	/* Handle packets until there are no more or we encounter an error. */
	do {
		/* Grab a packet. */
		if (wire_readpacket_peek(D->readq, &P))
			goto fail;

		/* Exit the loop if no packet is available. */
		if (P.buf == NULL)
			break;

		/* Bake a cookie. */
		if ((F = mpool_forwardee_malloc()) == NULL)
			goto err0;
		F->D = D;
		F->ID = P.ID;

		/* Record the request type. */
		if (P.len >= 4)
			F->reqtype = be32dec(&P.buf[0]);
		else
			F->reqtype = (uint32_t)(-1);

		/* Record when we send the request. */
		if (monoclock_get(&F->t_start)) {
			warnp("monoclock_get");
			goto err1;
		}

		/* Send the request to the target. */
		if (wire_requestqueue_add(D->Q, P.buf, P.len,
		    callback_gotresponse, F))
			goto err1;

		/* We have an additional outstanding request. */
		D->nrequests++;

		/* Consume the packet. */
		wire_readpacket_consume(D->readq, &P);
	} while (1);

	/* Wait for more packets to arrive. */
	if ((D->read_cookie = wire_readpacket_wait(D->readq,
	    callback_gotrequests, D)) == NULL) {
		warnp("Error reading request from connection");
		goto err0;
	}

	/* Success! */
	return (0);

fail:
	/*
	 * Something went wrong with the source connection -- either it failed
	 * while we were waiting for a packet, or we read a corrupted packet.
	 * Either way, we're not going to try reading any more packets -- and
	 * once the currently in-flight requests have had responses, we'll
	 * return zero from dispatch_alive.
	 */

	/* Return success; the connection will be reaped later. */
	return (0);

err1:
	free(F);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_gotresponse(void * cookie, uint8_t * buf, size_t buflen)
{
	struct forwardee * F = cookie;
	struct dispatch_state * D = F->D;
	struct wire_packet P;
	struct timeval t_end;
	uint64_t ID = F->ID;

	/* Get the completion time and compute request duration. */
	if (monoclock_get(&t_end))
		goto err1;

	/* Record the request duration. */
	if (perfstats_add(D->P, F->reqtype, timeval_diff(F->t_start, t_end)))
		goto err1;

	/* This request is no longer in flight. */
	D->nrequests--;

	/* Free the cookie. */
	mpool_forwardee_free(F);

	/* Did the request fail? */
	if (buf == NULL)
		goto failed;

	/* Send the response back to the client. */
	P.ID = ID;
	P.buf = buf;
	P.len = buflen;
	if (wire_writepacket(D->writeq, &P))
		goto err0;

	/* Success! */
	return (0);

failed:
	/*
	 * Stop reading requests -- if the target failed we won't be able to do
	 * anything useful with them anyway!  Once in-flight requests have been
	 * failed, dispatch_alive will return zero.
	 */
	if (D->read_cookie != NULL) {
		wire_readpacket_wait_cancel(D->read_cookie);
		D->read_cookie = NULL;
	}

	/* The failed request has been successfully handled. */
	return (0);

err1:
	mpool_forwardee_free(F);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_alive(D):
 * Return non-zero if the dispatcher with state ${D} is still alive.
 */
int
dispatch_alive(struct dispatch_state * D)
{

	return ((D->accept_cookie != NULL) || (D->read_cookie != NULL) ||
	    (D->nrequests > 0));
}

/**
 * dispatch_done(D):
 * Clean up the dispatcher state ${D}.
 */
void
dispatch_done(struct dispatch_state * D)
{

	/* Sanity-check. */
	assert(D->accept_cookie == NULL);
	assert(D->read_cookie == NULL);
	assert(D->nrequests == 0);

	/* Free the buffered reader and writer. */
	netbuf_read_free(D->readq);
	netbuf_write_free(D->writeq);

	/* Close the socket. */
	close(D->s);

	/* Free memory. */
	free(D);
}

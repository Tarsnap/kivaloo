#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpool.h"
#include "netbuf.h"
#include "network.h"
#include "wire.h"
#include "warnp.h"

#include "dispatch.h"

/* Dispatcher state. */
struct dispatch_state {
	/* Listening sockets. */
	struct sock_listen * sock_listen;	/* Listening sockets. */
	size_t nsock_listen;			/* # listening sockets. */

	/* Active sockets. */
	struct sock_active * sock_active;	/* Active sockets. */
	size_t nsock_active;			/* # active sockets. */
	size_t nsock_active_max;		/* Max # active sockets. */

	/* Request queue. */
	struct wire_requestqueue * Q;		/* Connected to target. */
	int failed;				/* Q has failed. */
};

/* Listening socket. */
struct sock_listen {
	struct dispatch_state * dstate;		/* Dispatcher. */
	int s;					/* Listening socket. */
	void * accept_cookie;			/* From network_accept. */
};

/* Connected client. */
struct sock_active {
	/* Bookkeeping. */
	struct dispatch_state * dstate;		/* Dispatcher. */
	struct sock_active * next;		/* Next in linked list. */
	struct sock_active * prev;		/* Previous in linked list. */

	/* The connection. */
	int s;					/* Connected socket. */
	struct netbuf_read * readq;		/* Packet read queue. */
	struct netbuf_write * writeq;		/* Packet write queue. */
	void * read_cookie;			/* Packet read cookie. */
	size_t nrequests;			/* # responses we owe. */
};

/* In-flight request state. */
struct forwardee {
	struct sock_active * conn;		/* Request origin. */
	uint64_t ID;				/* Request ID. */
};

MPOOL(forwardee, struct forwardee, 32768);

static void accept_stop(struct dispatch_state *);
static int accept_start(struct dispatch_state *);
static int callback_gotconn(void *, int);
static int readreq(struct sock_active *);
static int callback_gotrequests(void *, int);
static int callback_gotresponse(void *, uint8_t *, size_t);
static int reqdone(struct sock_active *);
static int dropconn(struct sock_active *);

static void
accept_stop(struct dispatch_state * dstate)
{
	struct sock_listen * L;
	size_t i;

	/* Iterate through the sockets, cancelling any accepts. */
	for (i = 0; i < dstate->nsock_listen; i++) {
		L = &dstate->sock_listen[i];
		if (L->accept_cookie != NULL) {
			network_accept_cancel(L->accept_cookie);
			L->accept_cookie = NULL;
		}
	}
}

static int
accept_start(struct dispatch_state * dstate)
{
	struct sock_listen * L;
	size_t i;

	/* Make sure we don't have any in-progress accepts. */
	for (i = 0; i < dstate->nsock_listen; i++) {
		L = &dstate->sock_listen[i];
		if (L->accept_cookie != NULL) {
			warn0("Already trying to accept a connection!");
			goto err0;
		}
	}

	/* Try to accept connections. */
	for (i = 0; i < dstate->nsock_listen; i++) {
		L = &dstate->sock_listen[i];
		if ((L->accept_cookie =
		    network_accept(L->s, callback_gotconn, L)) == NULL)
			goto err1;
	}

	/* Success! */
	return (0);

err1:
	/* Cancel the accepts we started. */
	accept_stop(dstate);
err0:
	/* Failure! */
	return (-1);
}

static int
callback_gotconn(void * cookie, int s)
{
	struct sock_listen * L = cookie;
	struct dispatch_state * dstate = L->dstate;
	struct sock_active * S;

	/* This listener is no longer accepting. */
	L->accept_cookie = NULL;

	/* Check if the accept failed. */
	if (s == -1) {
		warnp("Error accepting connection");
		goto err0;
	}

	/* Stop trying to accept connections. */
	accept_stop(dstate);

	/* Allocate an active connection structure. */
	if ((S = malloc(sizeof(struct sock_active))) == NULL)
		goto err1;
	S->dstate = dstate;
	S->next = NULL;
	S->prev = NULL;
	S->s = s;
	S->read_cookie = NULL;
	S->nrequests = 0;

	/* Make the accepted connection non-blocking. */
	if (fcntl(S->s, F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make connection non-blocking");
		goto err2;
	}

	/*
	 * Create a buffered writer for the connection.  We don't care to find
	 * out if we failed to write responses back to the incoming connection;
	 * we can't drop the connection until all our responses have come back
	 * from the target server, and once that happens the connection will
	 * be dropped automatically (since if we can't write, we won't be able
	 * to read either).
	 */
	if ((S->writeq = netbuf_write_init(S->s, NULL, NULL)) == NULL) {
		warnp("Cannot create packet write queue");
		goto err2;
	}

	/* Create a buffered reader for the connection. */
	if ((S->readq = netbuf_read_init(S->s)) == NULL) {
		warn0("Cannot create packet read queue");
		goto err3;
	}

	/* Wait for request packets to arrive. */
	if (readreq(S))
		goto err4;

	/* Add this connection to the list. */
	S->next = dstate->sock_active;
	if (S->next != NULL)
		S->next->prev = S;
	dstate->sock_active = S;

	/* We have a connection.  Do we want more? */
	if (++dstate->nsock_active < dstate->nsock_active_max) {
		if (accept_start(dstate))
			goto err0;
	}

	/* Success! */
	return (0);

err4:
	netbuf_read_free(S->readq);
err3:
	netbuf_write_free(S->writeq);
err2:
	free(S);
err1:
	close(s);
err0:
	/* Failure! */
	return (-1);
}

static int
readreq(struct sock_active * S)
{

	/* We shouldn't be waiting yet. */
	assert(S->read_cookie == NULL);

	/* Wait for request packets to arrive. */
	if ((S->read_cookie =
	    wire_readpacket_wait(S->readq, callback_gotrequests, S)) == NULL) {
		warnp("Error waiting for requests to arrive");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
readreq_cancel(struct sock_active * S)
{

	/* Stop waiting for requests. */
	wire_readpacket_wait_cancel(S->read_cookie);
	S->read_cookie = NULL;

	/* Drop the connection if it is now dead. */
	if ((S->nrequests == 0) && dropconn(S))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_gotrequests(void * cookie, int status)
{
	struct sock_active * S = cookie;
	struct dispatch_state * dstate = S->dstate;
	struct wire_packet P;
	struct forwardee * F;

	/* We're not waiting for a packet to be available any more. */
	S->read_cookie = NULL;

	/* If the wait failed, the connection is dying. */
	if (status)
		goto fail;

	/* Handle packets until there are no more or we encounter an error. */
	do {
		/* Grab a packet. */
		if (wire_readpacket_peek(S->readq, &P))
			goto fail;

		/* Exit the loop if no packet is available. */
		if (P.buf == NULL)
			break;

		/* Bake a cookie. */
		if ((F = mpool_forwardee_malloc()) == NULL)
			goto err0;
		F->ID = P.ID;
		F->conn = S;

		/* Send the request to the target. */
		if (wire_requestqueue_add(dstate->Q, P.buf, P.len,
		    callback_gotresponse, F))
			goto err1;

		/* We have an additional outstanding request. */
		S->nrequests++;

		/* Consume the packet. */
		wire_readpacket_consume(S->readq, &P);
	} while (1);

	/* Wait for more packets to arrive. */
	if (readreq(S))
		goto err0;

	/* Sucess! */
	return (0);

fail:
	/* If we have no requests in progress, drop the connection. */
	if (S->nrequests == 0) {
		if (dropconn(S))
			goto err0;
	}

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
	struct sock_active * S = F->conn;
	struct sock_active * S_next;
	struct dispatch_state * dstate = S->dstate;
	struct wire_packet P;

	/* Did this request fail? */
	if (buf == NULL)
		goto failed;

	/* Send the response back to the client. */
	P.ID = F->ID;
	P.buf = buf;
	P.len = buflen;
	if (wire_writepacket(S->writeq, &P))
		goto err1;

	/* Free the cookie. */
	mpool_forwardee_free(F);

	/* We've finished with a request. */
	if (reqdone(S))
		goto err0;

	/* Success! */
	return (0);

failed:
	/* Free our cookie. */
	mpool_forwardee_free(F);

	/* We've finished with a request. */
	if (reqdone(S))
		goto err0;

	/* Stop trying to accept connections. */
	accept_stop(dstate);

	/* The connection to the upstream server has failed. */
	dstate->failed = 1;

	/* Stop reading requests from connections. */
	for (S = dstate->sock_active; S != NULL; S = S_next) {
		S_next = S->next;
		if ((S->read_cookie != NULL) && readreq_cancel(S))
			goto err0;
	}

	/* The failed request has been successfully handled. */
	return (0);

err1:
	mpool_forwardee_free(F);
err0:
	/* Failure! */
	return (-1);
}

static int
reqdone(struct sock_active * S)
{

	/* We've finished a request. */
	S->nrequests--;

	/* Is this connection dead? */
	if ((S->nrequests == 0) && (S->read_cookie == NULL)) {
		if (dropconn(S))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
dropconn(struct sock_active * S)
{
	struct dispatch_state * dstate = S->dstate;

	/* Sanity check. */
	assert(S->read_cookie == NULL);
	assert(S->nrequests == 0);

	/* Detach from the dispatcher. */
	if (S->prev == NULL)
		dstate->sock_active = S->next;
	else
		S->prev->next = S->next;
	if (S->next != NULL)
		S->next->prev = S->prev;
	if (dstate->nsock_active-- == dstate->nsock_active_max) {
		if (accept_start(dstate))
			goto err2;
	}

	/* Free the buffered reader. */
	netbuf_read_free(S->readq);

	/* Free the buffered writer. */
	netbuf_write_free(S->writeq);

	/* Close the socket. */
	while (close(S->s)) {
		if (errno == EINTR)
			continue;
		warnp("close");
		goto err1;
	}

	/* Free the connection state. */
	free(S);

	/* Success! */
	return (0);

err2:
	netbuf_read_free(S->readq);
	netbuf_write_free(S->writeq);
	close(S->s);
err1:
	free(S);

	/* Failure! */
	return (-1);
}

/**
 * dispatch_init(socks, nsocks, Q, maxconn):
 * Initialize a dispatcher to accept connections from the listening sockets
 * ${socks}[0 .. ${nsocks} - 1] (but no more than ${maxconn} at once) and
 * shuttle requests/responses to/from the request queue ${Q}.
 */
struct dispatch_state *
dispatch_init(const int * socks, size_t nsocks,
    struct wire_requestqueue * Q, size_t maxconn)
{
	struct dispatch_state * dstate;
	size_t i;

	/* Bake a cookie. */
	if ((dstate = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;
	dstate->nsock_listen = nsocks;
	dstate->sock_active = NULL;
	dstate->nsock_active = 0;
	dstate->nsock_active_max = maxconn;
	dstate->Q = Q;
	dstate->failed = 0;

	/* Allocate an array of listeners. */
	if ((dstate->sock_listen =
	    malloc(nsocks * sizeof(struct sock_listen))) == NULL)
		goto err1;
	for (i = 0; i < nsocks; i++) {
		dstate->sock_listen[i].dstate = dstate;
		dstate->sock_listen[i].s = socks[i];
		dstate->sock_listen[i].accept_cookie = NULL;
	}

	/* Start accepting connections. */
	if (accept_start(dstate))
		goto err2;

	/* Success! */
	return (dstate);

err2:
	free(dstate->sock_listen);
err1:
	free(dstate);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * dispatch_alive(dstate):
 * Return non-zero if the dispatcher with state ${dstate} is still alive.
 */
int
dispatch_alive(struct dispatch_state * dstate)
{

	/*
	 * The dispatcher is alive if its connection to the target has not
	 * failed, or if it has any connections to clients (i.e., if they
	 * haven't been cleaned up yet).
	 */
	return ((dstate->failed == 0) || (dstate->nsock_active > 0));
}

/**
 * dispatch_done(dstate):
 * Clean up the dispatcher state ${dstate}.
 */
void
dispatch_done(struct dispatch_state * dstate)
{

	/* Sanity-check. */
	assert(dstate->failed == 1);
	assert(dstate->sock_active == NULL);
	assert(dstate->nsock_active == 0);

	/* Free memory. */
	free(dstate->sock_listen);
	free(dstate);
}

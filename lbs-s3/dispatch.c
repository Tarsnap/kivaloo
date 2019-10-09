#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "asprintf.h"
#include "netbuf.h"
#include "network.h"
#include "proto_lbs.h"
#include "warnp.h"
#include "wire.h"

#include "s3state.h"

#include "dispatch.h"

/* State of the work dispatcher. */
struct dispatch_state {
	/* S3 state. */
	struct s3state * S;

	/* Connection management. */
	int accepting;			/* We are waiting for a connection. */
	int sconn;			/* The current connection. */
	struct netbuf_write * writeq;	/* Buffered writer. */
	struct netbuf_read * readq;	/* Buffered reader. */
	void * read_cookie;		/* Request read cookie. */
	size_t npending;		/* # responses we owe. */
	int appendip;			/* An APPEND is in progress. */
};

static int callback_accept(void *, int);
static int callback_get(void *, struct proto_lbs_request *,
    const uint8_t *, size_t);
static int callback_append(void *, struct proto_lbs_request *, uint64_t);

/* The connection is dying.  Help speed up the process. */
static int
dropconnection(void * cookie)
{
	struct dispatch_state * D = cookie;

	/* If we're waiting for a request to arrive, stop waiting. */
	if (D->read_cookie != NULL) {
		wire_readpacket_wait_cancel(D->read_cookie);
		D->read_cookie = NULL;
	}

	/*
	 * Note: Since we do not cancel in-progress requests, they will
	 * continue and will at some point complete and attempt to write
	 * their responses.  This may result in writes to a failed buffered
	 * writer, but we don't care; the buffered writer will ignore them.
	 */

	/* Success!  (We can't fail -- but netbuf_write doesn't know that.) */
	return (0);
}

/* Read and dispatch incoming request(s). */
static int
gotrequest(void * cookie, int status)
{
	struct dispatch_state * D = cookie;
	struct proto_lbs_request * R;

	/* We're no longer waiting for a packet to arrive. */
	D->read_cookie = NULL;

	/* If the wait failed, the connection is dead. */
	if (status)
		goto drop;

	/* Read packets until there are no more or an error occurs. */
	do {
		/* Allocate space for a request. */
		if ((R = malloc(sizeof(struct proto_lbs_request))) == NULL)
			goto err0;

		/* Attempt to read a request. */
		if (proto_lbs_request_read(D->readq, R))
			goto drop1;

		/* If we have no request, stop looping. */
		if (R->type == PROTO_LBS_NONE)
			break;

		/* Handle the request. */
		switch (R->type) {
		case PROTO_LBS_PARAMS:
			warn0("PROTO_LBS_PARAMS is not implemented in lbs-s3");
			warn0("Update to a newer version of kvlds");
			goto drop1;
		case PROTO_LBS_PARAMS2:
			if (proto_lbs_response_params2(D->writeq, R->ID,
			    D->S->blklen, D->S->nextblk, D->S->lastblk))
				goto err1;
			free(R);
			break;
		case PROTO_LBS_GET:
			D->npending += 1;
			if (s3state_get(D->S, R, callback_get, D))
				goto err1;
			break;
		case PROTO_LBS_APPEND:
			if (R->r.append.blklen != D->S->blklen)
				goto drop2;
			if ((R->r.append.blkno != D->S->nextblk) ||
			    (D->appendip != 0)) {
				if (proto_lbs_response_append(D->writeq,
				    R->ID, 1, 0))
					goto err2;
				break;
			}
			D->npending += 1;
			D->appendip = 1;
			if (s3state_append(D->S, R, callback_append, D))
				goto err1;
			break;
		case PROTO_LBS_FREE:
			if (s3state_gc(D->S, R->r.free.blkno))
				goto err1;
			if (proto_lbs_response_free(D->writeq, R->ID))
				goto err1;
			free(R);
			break;
		default:
			/* proto_lbs_request_read broke. */
			assert(0);
		}
	} while (1);

	/* Free the (unused) request structure. */
	free(R);

	/* Wait for more requests to arrive. */
	if ((D->read_cookie = wire_readpacket_wait(D->readq,
	    gotrequest, D)) == NULL) {
		warnp("Error reading request from connection");
		goto err0;
	}

	/* Success! */
	return (0);

drop2:
	free(R->r.append.buf);
drop1:
	free(R);
drop:
	/* We didn't get a valid request.  Drop the connection. */
	dropconnection(D);

	/* All is good. */
	return (0);

err2:
	free(R->r.append.buf);
err1:
	free(R);
err0:
	/* Failure! */
	return (-1);
}

/* Send a GET response back. */
static int
callback_get(void * cookie, struct proto_lbs_request * R,
    const uint8_t * buf, size_t blklen)
{
	struct dispatch_state * D = cookie;
	int status;
	int rc;

	/* Does the block exist? */
	if (buf != NULL)
		status = 0;
	else
		status = 1;

	/* Sanity check. */
	assert(blklen <= UINT32_MAX);

	/* Send a response back. */
	rc = proto_lbs_response_get(D->writeq, R->ID, status, (uint32_t)blklen,
	    buf);

	/* Free the request. */
	free(R);

	/* This request is done. */
	D->npending -= 1;

	/* Return success/failure from response write. */
	return (rc);
}

/* Send an APPEND response back. */
static int
callback_append(void * cookie, struct proto_lbs_request * R, uint64_t nextblk)
{
	struct dispatch_state * D = cookie;
	int rc;

	/* Send a response back. */
	rc = proto_lbs_response_append(D->writeq, R->ID, 0, nextblk);

	/* Free the request. */
	free(R->r.append.buf);
	free(R);

	/* This request is now done. */
	D->npending -= 1;
	D->appendip = 0;

	/* Return success/failure from response write. */
	return (rc);
}

/**
 * dispatch_accept(S, s):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for the S3 state ${S}.
 */
struct dispatch_state *
dispatch_accept(struct s3state * S, int s)
{
	struct dispatch_state * D;

	/* Allocate space for dispatcher state. */
	if ((D = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;

	/* Initialize dispatcher. */
	D->S = S;

	/* Accept a connection. */
	D->accepting = 1;
	if (network_accept(s, callback_accept, D) == NULL)
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

	/* We have a socket. */
	if ((D->sconn = s) == -1) {
		warnp("Error accepting connection");
		goto err0;
	}

	/* Make the accepted connection non-blocking. */
	if (fcntl(D->sconn, F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make connection non-blocking");
		goto err1;
	}

	/* Create a buffered writer for the connection. */
	if ((D->writeq = netbuf_write_init(D->sconn,
	    dropconnection, D)) == NULL) {
		warnp("Cannot create packet write queue");
		goto err1;
	}

	/* Create a buffered reader for the connection. */
	if ((D->readq = netbuf_read_init(D->sconn)) == NULL) {
		warnp("Cannot create packet read queue");
		goto err2;
	}

	/* Wait for a request to arrive. */
	if ((D->read_cookie = wire_readpacket_wait(D->readq,
	    gotrequest, D)) == NULL) {
		warnp("Error reading request from connection");
		goto err3;
	}

	/* We are no longer waiting for a connection. */
	D->accepting = 0;

	/* No requests are pending yet. */
	D->npending = 0;
	D->appendip = 0;

	/* Success! */
	return (0);

err3:
	netbuf_read_free(D->readq);
err2:
	netbuf_write_free(D->writeq);
err1:
	close(D->sconn);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_alive(D):
 * Return non-zero iff the current connection being handled by the dispatcher
 * ${D} is still alive (if it is reading requests, has requests queued, is
 * processing requests, has responses queued up to be sent back, et cetera).
 */
int
dispatch_alive(struct dispatch_state * D)
{

	return ((D->accepting != 0) ||
	    (D->read_cookie != NULL) ||
	    (D->npending > 0));
}

/**
 * dispatch_done(D):
 * Clean up the dispatch state ${D}.  The function dispatch_alive(${D}) must
 * have previously returned zero.
 */
int
dispatch_done(struct dispatch_state * D)
{

	/* Sanity check. */
	assert(D->accepting == 0);
	assert(D->read_cookie == NULL);
	assert(D->npending == 0);

	/* Free the buffered reader for the connection. */
	netbuf_read_free(D->readq);

	/* Free the buffered writer for the connection. */
	netbuf_write_free(D->writeq);

	/* Close the connection. */
	while (close(D->sconn)) {
		if (errno == EINTR)
			continue;
		warnp("close");
		goto err0;
	}

	/* Free allocated memory. */
	free(D);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

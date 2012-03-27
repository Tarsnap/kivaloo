#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include "asprintf.h"
#include "http.h"
#include "network.h"
#include "proto_s3.h"
#include "s3_request.h"
#include "s3_request_queue.h"
#include "s3_verifyetag.h"
#include "warnp.h"
#include "wire.h"

#include "dispatch.h"

/* In-progress request. */
struct request {
	struct dispatch_state * D;	/* Dispatch state we belong to. */
	struct request * prev;		/* Previous request or NULL. */
	struct request * next;		/* Next request or NULL. */
	struct proto_s3_request R;	/* kivaloo-S3 protocol request. */
	struct s3_request req;		/* S3 request. */
	char * path;			/* "/object". */
	char * range;			/* "bytes=X-Y". */
	size_t maxrlen;			/* Maximum response length. */
	struct http_header hdr;		/* For Range: if needed. */
};

/* State of the work dispatcher. */
struct dispatch_state {
	/* S3 request queue. */
	struct s3_request_queue * Q;

	/* In-progress requests. */
	struct request * ip_head;
	struct request * ip_tail;

	/* Connection management. */
	int accepting;			/* We are waiting for a connection. */
	int sconn;			/* The current connection. */
	struct netbuf_write * writeq;	/* Buffered writer. */
	struct netbuf_read * readq;	/* Buffered reader. */
	void * read_cookie;		/* Request read cookie. */
};

static int callback_accept(void *, int);
static int callback_response(void *, struct http_response *);

/* Remove a request from the in-progress list. */
static void
request_dequeue(struct request * R)
{
	struct dispatch_state * D = R->D;

	/* Free the contents of the kivaloo-S3 request structure. */
	proto_s3_request_free(&R->R);

	/* Free extra allocations in the S3 request structure. */
	free(R->path);
	free(R->range);

	/* Remove from the linked list. */
	if (R->prev == NULL) {
		D->ip_head = R->next;
	} else {
		R->prev->next = R->next;
	}
	if (R->next == NULL) {
		D->ip_tail = R->prev;
	} else {
		R->next->prev = R->prev;
	}

	/* Free the request structure. */
	free(R);
}

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
	 * Flush the S3 request queue: If the connection is dying, we won't be
	 * able to send responses back, so there's no point processing the
	 * requests.
	 */
	s3_request_queue_flush(D->Q);

	/* Free the list of in-progress requests. */
	while (D->ip_head != NULL)
		request_dequeue(D->ip_head);

	/* Success!  (We can't fail -- but netbuf_write doesn't know that.) */
	return (0);
}

/* Read and dispatch incoming request(s). */
static int
gotrequest(void * cookie, int status)
{
	struct dispatch_state * D = cookie;
	struct request * R;

	/* We're no longer waiting for a packet to arrive. */
	D->read_cookie = NULL;

	/* If the wait failed, the connection is dead. */
	if (status)
		goto drop;

	/* Read packets until there are no more or an error occurs. */
	do {
		/* Allocate space for a request. */
		if ((R = malloc(sizeof(struct request))) == NULL)
			goto err0;
		R->D = D;

		/* Attempt to read a request. */
		if (proto_s3_request_read(D->readq, &R->R))
			goto drop1;

		/* If we have no request, stop looping. */
		if (R->R.type == PROTO_S3_NONE)
			break;

		/* Fill in the bucket and path fields. */
		R->req.bucket = R->R.bucket;
		if (asprintf(&R->path, "/%s", R->R.object) == -1)
			goto err1;
		R->req.path = R->path;

		/* Fill in default S3 request parameters. */
		R->req.nheaders = 0;
		R->req.headers = NULL;
		R->req.bodylen = 0;
		R->req.body = NULL;
		R->maxrlen = 0;
		R->range = NULL;

		/* Construct S3 request. */
		switch (R->R.type) {
		case PROTO_S3_PUT:
			/* PUT has a body and body length. */
			R->req.method = "PUT";
			R->req.bodylen = R->R.r.put.len;
			R->req.body = R->R.r.put.buf;
			break;
		case PROTO_S3_GET:
			/* GET has a maximum read length. */
			R->req.method = "GET";
			R->maxrlen = R->R.r.get.maxlen;
			break;
		case PROTO_S3_RANGE:
			/* Construct a Range header. */
			if (asprintf(&R->range, "bytes=%" PRIu64 "-%" PRIu64,
			    (uint64_t)(R->R.r.range.offset),
			    (uint64_t)(R->R.r.range.offset) +
				(uint64_t)(R->R.r.range.len) - 1) == -1)
				goto err2;

			/* RANGE is a GET with a Range header. */
			R->req.method = "GET";
			R->maxrlen = R->R.r.range.len;
			R->hdr.header = "Range";
			R->hdr.value = R->range;
			R->req.nheaders = 1;
			R->req.headers = &R->hdr;
			break;
		case PROTO_S3_HEAD:
			/* HEAD has no parameters. */
			R->req.method = "HEAD";
			break;
		case PROTO_S3_DELETE:
			/* DELETE has no parameters. */
			R->req.method = "DELETE";
			break;
		default:
			/* proto_s3_request_read broke. */
			assert(0);
		}

		/* Add the request to the S3 queue. */
		if (s3_request_queue(D->Q, &R->req, R->maxrlen,
		    callback_response, R))
			goto err3;

		/* Add to the linked list. */
		if ((R->prev = D->ip_tail) == NULL) {
			D->ip_head = R;
		} else {
			R->prev->next = R;
		}
		R->next = NULL;
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

drop1:
	free(R);
drop:
	/* We didn't get a valid request.  Drop the connection. */
	dropconnection(D);

	/* All is good. */
	return (0);

err3:
	free(R->range);
err2:
	free(R->path);
err1:
	free(R);
err0:
	/* Failure! */
	return (-1);
}

/* We have an HTTP response. */
static int
callback_response(void * cookie, struct http_response * res)
{
	struct request * R = cookie;
	struct dispatch_state * D = R->D;
	const char * s_clen;
	uint32_t clen;

	/* Send appropriate response back to the client. */
	switch (R->R.type) {
	case PROTO_S3_PUT:
		if (proto_s3_response_put(D->writeq, R->R.ID, res->status))
			goto err1;
		break;
	case PROTO_S3_GET:
		/* Verify that we have a body and the ETag is correct. */
		if ((res->body == NULL) || (s3_verifyetag(res) == 0))
			res->status = 0;

		/* Send the response. */
		if (proto_s3_response_get(D->writeq, R->R.ID, res->status,
		    res->bodylen, res->body))
			goto err1;
		break;
	case PROTO_S3_RANGE:
		if (proto_s3_response_range(D->writeq, R->R.ID, res->status,
		    res->bodylen, res->body))
			goto err1;
		break;
	case PROTO_S3_HEAD:
		/* Look for a Content-Length header. */
		if ((s_clen = http_findheader(res->headers,
		    res->nheaders, "Content-Length")) == NULL) {
			/* We have no Content-Length header. */
			clen = -1;
		} else {
			clen = strtoull(s_clen, NULL, 0);
		}

		/* Send the response. */
		if (proto_s3_response_head(D->writeq, R->R.ID, res->status,
		    clen))
			goto err1;
		break;
	case PROTO_S3_DELETE:
		if (proto_s3_response_delete(D->writeq, R->R.ID, res->status))
			goto err1;
	}

	/* Free the response body buffer. */
	free(res->body);

	/* Remove this request from the in-progress list. */
	request_dequeue(R);

	/* Success! */
	return (0);

err1:
	free(res->body);
	request_dequeue(R);

	/* Failure! */
	return (-1);
}

/**
 * dispatch_accept(Q, s):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for the S3 request queue ${Q}.
 */
struct dispatch_state *
dispatch_accept(struct s3_request_queue * Q, int s)
{
	struct dispatch_state * D;

	/* Allocate space for dispatcher state. */
	if ((D = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;

	/* Initialize dispatcher. */
	D->Q = Q;
	D->ip_head = D->ip_tail = NULL;

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
	    (D->read_cookie != NULL));
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
	assert(D->ip_head == NULL);
	assert(D->ip_tail == NULL);

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

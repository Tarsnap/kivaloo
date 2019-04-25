#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "dynamodb_kv.h"
#include "dynamodb_request_queue.h"
#include "http.h"
#include "netbuf.h"
#include "network.h"
#include "proto_dynamodb_kv.h"
#include "warnp.h"
#include "wire.h"

#include "dispatch.h"

/* In-progress request. */
struct request {
	struct dispatch_state * D;	/* Dispatch state we belong to. */
	struct request * prev;		/* Previous request or NULL. */
	struct request * next;		/* Next request or NULL. */
	struct proto_ddbkv_request R;	/* kivaloo-dynamodb-kv request. */
	char * body;			/* DynamoDB request body. */
};

/* State of the work dispatcher. */
struct dispatch_state {
	/* DynamoDB request queues. */
	struct dynamodb_request_queue * QW;
	struct dynamodb_request_queue * QR;

	/* Target table. */
	const char * table;

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
request_dequeue(struct dispatch_state * D, struct request * R)
{

	/* Sanity-check linked list. */
	assert(D == R->D);

	/* Free the contents of the kivaloo-dynamodb-kv request. */
	proto_dynamodb_kv_request_free(&R->R);

	/* Free the DynamoDB request body we constructed. */
	free(R->body);

	/* Remove from the linked list. */
	if (D->ip_head == R) {
		assert(R->prev == NULL);
		D->ip_head = R->next;
	} else {
		assert(R->prev != NULL);
		R->prev->next = R->next;
	}
	if (D->ip_tail == R) {
		assert(R->next == NULL);
		D->ip_tail = R->prev;
	} else {
		assert(R->next != NULL);
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
	 * Flush the DynamoDB request queues: If the connection is dying,
	 * we won't be able to send responses back, so there's no point
	 * processing the requests in the first place.
	 */
	dynamodb_request_queue_flush(D->QW);
	dynamodb_request_queue_flush(D->QR);

	/* Free the list of in-progress requests. */
	while (D->ip_head != NULL)
		request_dequeue(D, D->ip_head);

	/* Success!  (We can't fail -- but netbuf_write doesn't know that.) */
	return (0);
}

/* Read and dispatch incoming request(s). */
static int
gotrequest(void * cookie, int status)
{
	struct dispatch_state * D = cookie;
	struct request * R;
	struct dynamodb_request_queue * Q;
	const char * op;
	size_t maxrlen;
	int prio;

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
		if (proto_dynamodb_kv_request_read(D->readq, &R->R))
			goto drop1;

		/* If we have no request, stop looping. */
		if (R->R.type == PROTO_DDBKV_NONE)
			break;

		/* Translate this to a DynamoDB request. */
		switch (R->R.type) {
		case PROTO_DDBKV_PUT:
			Q = D->QW;
			op = "PutItem";
			maxrlen = 1024;
			prio = 0;
			if ((R->body = dynamodb_kv_put(D->table, R->R.key,
			    R->R.buf, R->R.len)) == NULL)
				goto err1;
			break;
		case PROTO_DDBKV_GET:
			Q = D->QR;
			op = "GetItem";
			prio = 0;
			maxrlen = 1048576;
			if ((R->body =
			    dynamodb_kv_get(D->table, R->R.key)) == NULL)
				goto err1;
			break;
		case PROTO_DDBKV_GETC:
			Q = D->QR;
			op = "GetItem";
			prio = 0;
			maxrlen = 1048576;
			if ((R->body =
			    dynamodb_kv_getc(D->table, R->R.key)) == NULL)
				goto err1;
			break;
		case PROTO_DDBKV_DELETE:
			Q = D->QW;
			op = "DeleteItem";
			maxrlen = 1024;
			prio = 1;
			if ((R->body =
			    dynamodb_kv_delete(D->table, R->R.key)) == NULL)
				goto err1;
			break;
		default:
			/* proto_dynamodb_kv_request_read broke. */
			assert(0);
		}

		/* Add the request to the appropriate DynamoDB queue. */
		if (dynamodb_request_queue(Q, prio, op, R->body, maxrlen,
		    R->R.key, callback_response, R))
			goto err2;

		/* Add to the linked list. */
		if ((R->prev = D->ip_tail) == NULL) {
			D->ip_head = R;
		} else {
			R->prev->next = R;
		}
		R->next = NULL;
		D->ip_tail = R;
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

err2:
	free(R->body);
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
	uint8_t * vbuf;
	uint32_t vlen;
	int status;

	/* Did we succeed? */
	status = (res->status == 200) ? 0 : 1;

	/* Send appropriate response back to the client. */
	switch (R->R.type) {
	case PROTO_DDBKV_PUT:
	case PROTO_DDBKV_DELETE:	/* Has same response as PUT. */
		if (proto_dynamodb_kv_response_put(D->writeq, R->R.ID,
		    status))
			goto err1;
		break;
	case PROTO_DDBKV_GET:
	case PROTO_DDBKV_GETC:		/* Has same response as GET. */
		/* Extract the value. */
		if (dynamodb_kv_extractv(res->body, res->bodylen,
		    &vbuf, &vlen))
			goto err1;

		/* A success without data is a status of 2. */
		if ((status == 0) && (vbuf == NULL))
			status = 2;

		/* Send a response back. */
		if (proto_dynamodb_kv_response_get(D->writeq, R->R.ID,
		    status, vlen, vbuf))
			goto err2;

		/* Free the value. */
		free(vbuf);
		break;
	}

	/* Free the response body buffer. */
	free(res->body);

	/* Remove this request from the in-progress list. */
	request_dequeue(D, R);

	/* Success! */
	return (0);

err2:
	free(vbuf);
err1:
	free(res->body);
	request_dequeue(D, R);

	/* Failure! */
	return (-1);
}

/**
 * dispatch_accept(QW, QR, table, s):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for sending requests to the DynamoDB queues ${QW} (writes/deletes)
 * and ${QR} (reads) for operations on table ${table}.
 */
struct dispatch_state *
dispatch_accept(struct dynamodb_request_queue * QW,
    struct dynamodb_request_queue * QR, const char * table, int s)
{
	struct dispatch_state * D;

	/* Allocate space for dispatcher state. */
	if ((D = malloc(sizeof(struct dispatch_state))) == NULL)
		goto err0;

	/* Initialize dispatcher. */
	D->QW = QW;
	D->QR = QR;
	D->table = table;
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

	return ((D->accepting != 0) || (D->read_cookie != NULL));
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
	close(D->sconn);

	/* Free allocated memory. */
	free(D);

	/* Success! */
	return (0);
}

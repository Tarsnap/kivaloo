#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "logging.h"
#include "monoclock.h"
#include "s3_request.h"
#include "s3_serverpool.h"
#include "sock.h"
#include "sock_util.h"

#include "s3_request_queue.h"

/* Request. */
struct request {
	/* The queue to which we belong. */
	struct s3_request_queue * Q;

	/* Request parameters. */
	struct s3_request * request;
	size_t maxrlen;
	int (* callback)(void *, struct http_response *);
	void * cookie;

	/* Start time. */
	struct timeval t_start;

	/* Internal state. */
	struct sock_addr * addrs[2];	/* Endpoint address, NULL. */
	void * http_cookie;		/* Returned by s3_request. */

	/* Doubly-linked list -- either _queued_ or _ip_. */
	struct request * prev;
	struct request * next;
};

/* Queue of requests. */
struct s3_request_queue {
	char * key_id;
	char * key_secret;
	struct s3_serverpool * SP;
	struct logging_file * logfile;
	size_t reqsip_max;
	size_t reqsip;
	struct request * reqs_queued_head;
	struct request * reqs_queued_tail;
	struct request * reqs_ip_head;
	struct request * reqs_ip_tail;
};

static int poke(struct s3_request_queue *);

/**
 * callback_reqdone(cookie, res):
 * Process the HTTP response ${res} to the queued S3 request ${cookie}.
 */
static int
callback_reqdone(void * cookie, struct http_response * res)
{
	struct request * R = cookie;
	struct s3_request_queue * Q = R->Q;
	struct timeval t_end;
	long t_micros;
	char * addr;
	size_t rslen;
	int rc = 0;
	int rc2;

	/* Compute how long the request took. */
	if (monoclock_get(&t_end))
		rc = -1;
	t_micros = (t_end.tv_sec - R->t_start.tv_sec) * 1000000 +
	    (t_end.tv_usec - R->t_start.tv_usec);

	/* If we have a log file, log the S3 request. */
	if (Q->logfile != NULL) {
		/* Prettyprint the address we used. */
		addr = sock_addr_prettyprint(R->addrs[0]);

		/* Write to the log file. */
		if ((res != NULL) && (res->bodylen != (size_t)(-1)))
			rslen = res->bodylen;
		else
			rslen = 0;
		if (logging_printf(Q->logfile, "|%s|/%s%s|%d|%s|%ld|%zu|%zu",
		    R->request->method, R->request->bucket, R->request->path,
		    (res != NULL) ? res->status : 0,
		    (addr != NULL) ? addr : "(unknown)",
		    t_micros, R->request->bodylen, rslen) == -1)
			rc = -1;

		/* Free the address string. */
		free(addr);
	}

	/* This address has been tried. */
	sock_addr_free(R->addrs[0]);

	/* The number of in-progress requests has just decreased. */
	Q->reqsip -= 1;

	/* Remove from the in-progress queue. */
	if (R->next) {
		R->next->prev = R->prev;
	} else {
		Q->reqs_ip_tail = R->prev;
	}
	if (R->prev) {
		R->prev->next = R->next;
	} else {
		Q->reqs_ip_head = R->next;
	}

	/* If the HTTP connection failed, try this again later. */
	if (res == NULL)
		goto tryagain;

	/* If we got a 500 or 503 response, try this again later. */
	if ((res->status == 500) || (res->status == 503))
		goto tryagain;

	/* Send the response upstream. */
	rc2 = (R->callback)(R->cookie, res);
	rc = rc2 ? rc2 : rc;

	/* Free the request structure. */
	free(R);

	/* Launch another request if possible. */
	if (poke(Q))
		goto err0;

	/* Return status from upstream callback. */
	return (rc);

tryagain:
	/* Add this request back to the queue. */
	R->prev = Q->reqs_queued_tail;
	R->next = NULL;
	if (R->prev == NULL) {
		Q->reqs_queued_head = R;
	} else {
		R->prev->next = R;
	}
	Q->reqs_queued_tail = R;

	/* Poke the queue. */
	if (poke(Q))
		goto err0;

	/* The failure has been successfully handled. */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * poke(Q):
 * If there is a request in the pending queue and we are not at the maximum
 * number of in-progress requests, attempt to launch another request.  On
 * error, no request has been launched.
 */
static int
poke(struct s3_request_queue * Q)
{
	struct request * R;

	/* If no requests are queued, do nothing. */
	if (Q->reqs_queued_head == NULL)
		goto done;

	/* If we're at the in-progress limit, do nothing. */
	if (Q->reqsip == Q->reqsip_max)
		goto done;

	/* Sanity-check. */
	assert(Q->reqsip < Q->reqsip_max);

	/* Grab the request at the head of the queue. */
	R = Q->reqs_queued_head;

	/* Grab an S3 endpoint address. */
	if ((R->addrs[0] = s3_serverpool_pick(Q->SP)) == NULL)
		goto err0;
	R->addrs[1] = NULL;

	/* Record when we send this request. */
	if (monoclock_get(&R->t_start))
		goto err0;

	/* Launch the S3 request. */
	if ((R->http_cookie = s3_request(R->addrs, Q->key_id, Q->key_secret,
	    R->request, R->maxrlen, callback_reqdone, R)) == NULL)
		goto err0;

	/* The number of in-progress requests has just increased. */
	Q->reqsip += 1;

	/* Remove from the pending queue... */
	if (R->next) {
		R->next->prev = NULL;
	} else {
		Q->reqs_queued_tail = NULL;
	}
	Q->reqs_queued_head = R->next;

	/* ... and place into the in-progress queue. */
	R->prev = Q->reqs_ip_tail;
	R->next = NULL;
	if (R->prev == NULL) {
		Q->reqs_ip_head = R;
	} else {
		R->prev->next = R;
	}
	Q->reqs_ip_tail = R;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * s3_request_queue_init(key_id, key_secret, conns):
 * Create an S3 request queue using the AWS Key ID ${key_id} and the Secret
 * Access Key ${key_secret} to perform up to ${conns} simultaneous requests.
 */
struct s3_request_queue *
s3_request_queue_init(const char * key_id, const char * key_secret,
    size_t conns)
{
	struct s3_request_queue * Q;

	/* Allocate a request queue structure. */
	if ((Q = malloc(sizeof(struct s3_request_queue))) == NULL)
		goto err0;

	/* Create a server pool structure. */
	if ((Q->SP = s3_serverpool_init()) == NULL)
		goto err1;

	/* Copy key_id and key_secret. */
	if ((Q->key_id = strdup(key_id)) == NULL)
		goto err2;
	if ((Q->key_secret = strdup(key_secret)) == NULL)
		goto err3;

	/* No log file yet. */
	Q->logfile = NULL;

	/* No requests queued or in progress. */
	Q->reqsip = 0;
	Q->reqs_queued_head = Q->reqs_queued_tail = NULL;
	Q->reqs_ip_head = Q->reqs_ip_tail = NULL;

	/* Record the maximum number of simultaneous requests. */
	Q->reqsip_max = conns;

	/* Success! */
	return (Q);

err3:
	free(Q->key_id);
err2:
	s3_serverpool_free(Q->SP);
err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * s3_request_queue_log(Q, F):
 * Log all S3 requests performed by the queue ${Q} to the log file ${F}.
 */
void
s3_request_queue_log(struct s3_request_queue * Q, struct logging_file * F)
{

	Q->logfile = F;
}

/**
 * s3_request_queue_addaddr(Q, addr, ttl):
 * Add the address ${addr} to the S3 request queue ${Q}, valid for the next
 * ${ttl} seconds.  The address ${addr} is copied and does not need to remain
 * valid after the call returns.
 */
int
s3_request_queue_addaddr(struct s3_request_queue * Q,
    const struct sock_addr * addr, int ttl)
{

	/* Add to the server pool. */
	return (s3_serverpool_add(Q->SP, addr, ttl));
}

/**
 * s3_request_queue(Q, request, maxrlen, callback, cookie):
 * Using the S3 request queue ${Q}, queue the S3 request ${request} to be
 * performed using a target address selected from those provided via the
 * s3_request_queue_addaddr function and the AWS Key ID and Secret Access Key
 * provided via the s3_request_queue_init function.  Requests which fail due
 * to the HTTP connection breaking or with HTTP 500 or 503 responses are
 * retried.  The S3 request structure ${request} must remain valid until the
 * callback is performed or the request queue is freed.  Behave identically to
 * http_request otherwise.
 */
int
s3_request_queue(struct s3_request_queue * Q, struct s3_request * request,
    size_t maxrlen,
    int (* callback)(void *, struct http_response *), void * cookie)
{
	struct request * R;

	/* Bake a cookie. */
	if ((R = malloc(sizeof(struct request))) == NULL)
		goto err0;
	R->Q = Q;
	R->request = request;
	R->maxrlen = maxrlen;
	R->callback = callback;
	R->cookie = cookie;
	R->http_cookie = NULL;

	/* Add to the end of the pending-requests queue. */
	R->prev = Q->reqs_queued_tail;
	R->next = NULL;
	if (R->prev == NULL) {
		Q->reqs_queued_head = R;
	} else {
		R->prev->next = R;
	}
	Q->reqs_queued_tail = R;

	/* Poke the queue. */
	if (poke(Q))
		goto err1;

	/* Success! */
	return (0);

err1:
	/* Take R back off the pending-requests queue. */
	if (R->prev != NULL) {
		R->prev->next = NULL;
	} else {
		Q->reqs_queued_head = NULL;
	}
	Q->reqs_queued_tail = R->prev;

	/* Free the request structure. */
	free(R);

err0:
	/* Failure! */
	return (-1);
}

/**
 * s3_request_queue_flush(Q):
 * Flush the S3 request queue ${Q}.  Any queued requests will be dropped; no
 * callbacks will be performed.
 */
void
s3_request_queue_flush(struct s3_request_queue * Q)
{
	struct request * R;

	/* Free the contents of the pending-requests queue. */
	while ((R = Q->reqs_queued_head) != NULL) {
		Q->reqs_queued_head = R->next;
		free(R);
	}

	/* Cancel in-progress requests and free them. */
	while ((R = Q->reqs_ip_head) != NULL) {
		http_request_cancel(R->http_cookie);
		Q->reqs_ip_head = R->next;
		sock_addr_free(R->addrs[0]);
		free(R);
	}
}

/**
 * s3_request_queue_free(Q):
 * Free the S3 request queue ${Q}.  Any queued requests will be dropped; no
 * callbacks will be performed.
 */
void
s3_request_queue_free(struct s3_request_queue * Q)
{

	/* Flush the queue. */
	s3_request_queue_flush(Q);

	/* Free the S3 keys. */
	memset(Q->key_secret, 0, strlen(Q->key_secret));
	free(Q->key_secret);
	free(Q->key_id);

	/* Free the server pool. */
	s3_serverpool_free(Q->SP);

	/* Free the request queue structure. */
	free(Q);
}

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dynamodb_request.h"
#include "events.h"
#include "http.h"
#include "insecure_memzero.h"
#include "json.h"
#include "logging.h"
#include "monoclock.h"
#include "ptrheap.h"
#include "serverpool.h"
#include "sock.h"
#include "sock_util.h"
#include "warnp.h"

#include "dynamodb_request_queue.h"

/**
 * Requests can be in three states:
 * 1. Waiting to be sent -- http_cookie and timeout_cookie are NULL.
 * 2. Request is in progress -- http_cookie and timeout_cookie are non-NULL.
 * 3. Request failed but we're waiting until out timer expires before we
 * allow it to be sent again -- http_cookie is NULL but timeout_cookie isn't.
 */

/* Request. */
struct request {
	struct dynamodb_request_queue * Q;
	const char * op;
	const uint8_t * body;
	size_t bodylen;
	size_t maxrlen;
	char * logstr;
	int (* callback)(void *, struct http_response *);
	void * cookie;
	struct sock_addr * addrs[2];
	void * http_cookie;
	void * timeout_cookie;
	size_t ntries;
	struct timeval t_start;
	int prio;
	uint64_t reqnum;
	size_t rc;
};

/* Queue of requests. */
struct dynamodb_request_queue {
	char * key_id;
	char * key_secret;
	char * region;
	struct serverpool * SP;
	double mu_capperreq;
	double spercap;
	double bucket_cap;
	double maxburst_cap;
	void * timer_cookie;
	void * immediate_cookie;
	size_t inflight;
	struct ptrheap * reqs;
	uint64_t reqnum;
	struct logging_file * logfile;
	double tmu;
	double tmud;
};

static int runqueue(struct dynamodb_request_queue *);

/* Callback from events_timer. */
static int
poke_timer(void * cookie)
{
	struct dynamodb_request_queue * Q = cookie;

	/* There is no timer callback pending any more. */
	Q->timer_cookie = NULL;

	/* Increase burst capacity. */
	Q->bucket_cap += 1.0;

	/* Run the queue. */
	return (runqueue(Q));
}

/* Callback from events_immediate. */
static int
poke_immediate(void * cookie)
{
	struct dynamodb_request_queue * Q = cookie;

	/* There is no immediate callback pending any more. */
	Q->immediate_cookie = NULL;

	/* Run the queue. */
	return (runqueue(Q));
}

/* Schedule an immediate callback if appropriate. */
static int
poke(struct dynamodb_request_queue * Q)
{

	/* Do we need an immediate callback? */
	if (Q->immediate_cookie == NULL) {
		if ((Q->immediate_cookie = events_immediate_register(
		    poke_immediate, Q, 0)) == NULL)
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Is this a ProvisionedThroughputExceededException? */
static int
isthrottle(struct http_response * res)
{
	size_t i;

	/*
	 * Search the body for "#ProvisionedThroughputExceededException".
	 * The AWS SDKs extract the "__type" field, split this on '#'
	 * characters, and look at the last element; we're guaranteed to
	 * catch anything they catch, and if someone can trigger HTTP 400
	 * responses which yield false positives, we don't really care -- the
	 * worst they can do is to prevent us from bursting requests.
	 */
#define SS "#ProvisionedThroughputExceededException"
	for (i = 0; i + strlen(SS) <= res->bodylen; i++) {
		if (memcmp(&res->body[i], SS, strlen(SS)) == 0)
			return (1);
	}
#undef SS
	return (0);
}

/* Log the (attempted) request. */
static int
logreq(struct logging_file * F, struct request * R,
    struct http_response * res, double capacity,
    struct timeval t_end)
{
	long t_micros;
	char * addr;
	int status;
	size_t bodylen;

	/* Compute how long the request took. */
	t_micros = (long)(t_end.tv_sec - R->t_start.tv_sec) * 1000000 +
	    t_end.tv_usec - R->t_start.tv_usec;

	/* Prettyprint the address we selected. */
	if ((addr = sock_addr_prettyprint(R->addrs[0])) == NULL)
		goto err0;

	/* Extract parameters from HTTP response. */
	if (res != NULL) {
		status = res->status;
		bodylen = res->bodylen;
	} else {
		status = 0;
		bodylen = 0;
	}

	/* Write to the log file. */
	if (logging_printf(F, "|%s|%s|%d|%s|%ld|%zu|%f",
	    R->op, R->logstr ? R->logstr : "",
	    status, addr, t_micros, bodylen, capacity))
		goto err1;

	/* Free string allocated by sock_addr_prettyprint. */
	free(addr);

	/* Success! */
	return (0);

err1:
	free(addr);
err0:
	/* Failure! */
	return (-1);
}

/* Extract ConsumedCapacity->CapacityUnits from returned JSON. */
static int
extractcapacity(struct http_response * res, double * pcap)
{
	const uint8_t * buf = res->body;
	const uint8_t * end = res->body + res->bodylen;
	char * capacity;
	size_t len;
	double c;

	/* Look for ConsumedCapacity. */
	buf = json_find(buf, end, "ConsumedCapacity");

	/* Look for CapacityUnits inside that. */
	buf = json_find(buf, end, "CapacityUnits");

	/* Figure out how long the numeric value is. */
	for (len = 0; &buf[len] < end; len++) {
		if (strchr("+-0123456789.eE", buf[len]) == NULL)
			break;
	}

	/* Extract it into a (NUL-terminated) string. */
	if ((capacity = malloc(len + 1)) == NULL)
		goto err0;
	memcpy(capacity, buf, len);
	capacity[len] = '\0';

	/* Parse the string. */
	c = strtod(capacity, NULL);
	if ((c < 0) || (c > 400)) {
		/*
		 * As specified right now, DynamoDB should never return a
		 * CapacityUnits outside [0, 400]; but just in case that
		 * changes in the future, print a warning but don't treat
		 * it as an error.
		 */
		warn0("Invalid DynamoDB CapacityUnits returned: %s",
		    capacity);
		c = 0.0;
	}

	/* Free the capacity string. */
	free(capacity);

	/* Return the parsed capacity. */
	*pcap = c;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Clean up an HTTP request. */
static int
done_http(struct request * R, int timedout)
{
	struct dynamodb_request_queue * Q = R->Q;
	struct timeval t_end;
	int rc = 0;

	/* Cancel the request if it hasn't completed and we timed out. */
	if (timedout && R->http_cookie) {
		http_request_cancel(R->http_cookie);
		if (Q->logfile) {
			if (monoclock_get(&t_end)) {
				warnp("monoclock_get");
				t_end = R->t_start;
				rc = -1;
			}
			if (logreq(Q->logfile, R, NULL, 0.0, t_end))
				rc = -1;
		}
	}

	/* If a request was in flight, it isn't any more. */
	if (R->http_cookie) {
		R->http_cookie = NULL;
		Q->inflight--;
		sock_addr_free(R->addrs[0]);
		R->addrs[0] = NULL;
	}

	/* Return status. */
	return (rc);
}

/* Callback from events_timer. */
static int
callback_timeout(void * cookie)
{
	struct request * R = cookie;
	struct dynamodb_request_queue * Q = R->Q;

	/* We are no longer waiting for a timer callback. */
	R->timeout_cookie = NULL;

	/* Clean up the HTTP request. */
	if (done_http(R, 1))
		goto err0;

	/* The priority of this request has changed. */
	ptrheap_decrease(Q->reqs, R->rc);

	/*
	 * Poke the queue -- this request has moved from "in progress" back
	 * to "waiting to be sent", so it might be possible to send it now.
	 */
	if (poke(Q))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback from dynamodb_request (aka. from http_request). */
static int
callback_reqdone(void * cookie, struct http_response * res)
{
	struct request * R = cookie;
	struct dynamodb_request_queue * Q = R->Q;
	struct timeval t_end;
	double treq;
	int rc = 0;
	double capacity = 0.0;

	/*
	 * If we have a response body, extract the number of capacity units
	 * used, and update our rolling average and current capacity.
	 */
	if ((res != NULL) && (res->bodylen > 0)) {
		if (extractcapacity(res, &capacity))
			rc = -1;
		if (capacity != 0.0) {
			Q->mu_capperreq +=
			    (capacity - Q->mu_capperreq) * 0.01;
			Q->bucket_cap -= capacity;
			if (Q->bucket_cap < 0.0)
				Q->bucket_cap = 0.0;
		}
	}

	/* Figure out how long this request took. */
	if (monoclock_get(&t_end)) {
		warnp("monoclock_get");
		t_end = R->t_start;
		rc = -1;
	}

	/* Optionally log this request. */
	if (Q->logfile) {
		if (logreq(Q->logfile, R, res, capacity, t_end))
			rc = -1;
	}

	/*
	 * This HTTP request has completed; we call done_http here rather
	 * than earlier because it frees the target address, which is printed
	 * to the request log.
	 */
	if (done_http(R, 0))
		rc = -1;

	/* What should we do with this response? */
	if ((res != NULL) && (res->status == 400) && isthrottle(res)) {
		/*
		 * We hit the throughput limits.  Zero out our estimate of
		 * the number of tokens in the bucket; we won't send any
		 * more requests until timer ticks add more tokens to the
		 * modelled bucket.
		 */
		Q->bucket_cap = 0.0;
	} else if ((res != NULL) && (res->status < 500)) {
		/*
		 * Anything which isn't an internal DynamoDB error or a
		 * rate limiting response is something we should pass back
		 * to the upstream code.
		 */

		/* Dequeue the request. */
		ptrheap_delete(Q->reqs, R->rc);

		/* Cancel the request timeout. */
		events_timer_cancel(R->timeout_cookie);

		/*
		 * Update request timeout statistics.  Following the strategy
		 * used in TCP, we compute exponential rolling averages for
		 * the mean and mean deviation; unlike TCP, we update our
		 * statistics even on retries, since we know which attempt
		 * succeeded.
		 */
		treq = (t_end.tv_sec - R->t_start.tv_sec) +
		    (t_end.tv_usec - R->t_start.tv_usec) * 0.000001;
		Q->tmu += (treq - Q->tmu) * 0.125;
		if (treq > Q->tmu)
			Q->tmud += ((treq - Q->tmu) - Q->tmud) * 0.25;
		else
			Q->tmud += ((Q->tmu - treq) - Q->tmud) * 0.25;

		/* Invoke the upstream callback. */
		if (rc) {
			(void)(R->callback)(R->cookie, res);
		} else {
			rc = (R->callback)(R->cookie, res);
		}

		/* Free the request; we're done with it now. */
		free(R->logstr);
		free(R);
	}

	/*
	 * If we didn't send the response upstream, the request is still on
	 * our queue with a timeout callback pending.  We're going to leave
	 * it that way -- we don't want to retry the failed request until
	 * the callback fires.
	 */

	/*
	 * Poke the queue.  If the request failed, it may be possible to
	 * re-issue it; if the request succeeded, we may have ceased to be
	 * at our in-flight limit and might be able to issue a new request;
	 * if we just hit our first congestion, we need to start a timer to
	 * add more tokens to our modelled bucket.
	 */
	if (poke(Q))
		rc = -1;

	/* Return status from callback, or our own success/failure. */
	return (rc);
}

/* Send a request. */
static int
sendreq(struct dynamodb_request_queue * Q, struct request * R)
{
	double timeo;

	/* Get a target address. */
	R->addrs[0] = serverpool_pick(Q->SP);

	/* Record start time. */
	if (monoclock_get(&R->t_start)) {
		warnp("monoclock_get");
		goto err1;
	}

	/*
	 * Compute a timeout; we start with a timeout equal to the mean times
	 * 1.5 plus four times the mean difference then double for each retry
	 * until we hit a maximum of 15 seconds.  This is the same as TCP
	 * except for the factor of 1.5; we include that due to TCP-over-TCP
	 * issues, since the loss of a single TCP segment will result in at
	 * least one extra network RTT.
	 */
	if (R->ntries < 20) {
		timeo = (Q->tmu * 1.5 + Q->tmud * 4) * (1 << R->ntries);
		if (timeo > 15.0)
			timeo = 15.0;
	} else {
		timeo = 15.0;
	}
	R->ntries++;

	/* Time out if we take too long. */
	if ((R->timeout_cookie =
	    events_timer_register_double(callback_timeout, R, timeo)) == NULL)
		goto err1;

	/* Send the request. */
	Q->inflight++;
	if ((R->http_cookie = dynamodb_request(R->addrs, Q->key_id,
	    Q->key_secret, Q->region, R->op, R->body, R->bodylen, R->maxrlen,
	    callback_reqdone, R)) == NULL)
		goto err2;

	/* The priority of this request has changed. */
	ptrheap_increase(Q->reqs, R->rc);

	/* Success! */
	return (0);

err2:
	events_timer_cancel(R->timeout_cookie);
	R->timeout_cookie = NULL;
err1:
	sock_addr_free(R->addrs[0]);
	R->addrs[0] = NULL;

	/* Failure! */
	return (-1);
}

/* Check if we need to do anything with the queue. */
static int
runqueue(struct dynamodb_request_queue * Q)
{
	struct request * R;

	/* Send requests as long as we have enough capacity. */
	while ((Q->inflight * Q->mu_capperreq < Q->maxburst_cap) &&
	    (Q->inflight * Q->mu_capperreq < Q->bucket_cap)) {
		/* Find the highest-priority request in the queue. */
		R = ptrheap_getmin(Q->reqs);

		/* Nothing left to send? */
		if ((R == NULL) || (R->timeout_cookie != NULL))
			break;

		/* Send the highest-priority request. */
		if (sendreq(Q, R))
			goto err0;
	}

	/* Do we need to (re)start the capacity-accumulation timer? */
	if ((Q->timer_cookie == NULL) &&
	    (Q->bucket_cap * Q->spercap < 300.0)) {
		if ((Q->timer_cookie = events_timer_register_double(
		        poke_timer, Q, Q->spercap)) == NULL)
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Record-comparison callback from ptrheap. */
static int
compar(void * cookie, const void * x, const void * y)
{
	const struct request * _x = x;
	const struct request * _y = y;

	(void)cookie; /* UNUSED */

	/* Is one of the requests in progress? */
	if ((_x->timeout_cookie != NULL) && (_y->timeout_cookie == NULL))
		return (1);
	if ((_x->timeout_cookie == NULL) && (_y->timeout_cookie != NULL))
		return (-1);

	/* Is one a higher priority? */
	if (_x->prio > _y->prio)
		return (1);
	if (_x->prio < _y->prio)
		return (-1);

	/* Sort in order of arrival. */
	if (_x->reqnum > _y->reqnum)
		return (1);
	else
		return (-1);
}

/* Cookie-recording callback from ptrheap. */
static void
setreccookie(void * cookie, void * ptr, size_t rc)
{
	struct request * R = ptr;

	(void)cookie; /* UNUSED */

	R->rc = rc;
}

/**
 * dynamodb_request_queue_init(key_id, key_secret, region, SP):
 * Create a DynamoDB request queue using AWS key id ${key_id} and secret key
 * ${key_secret} to make requests to DynamoDB in ${region}.  Obtain target
 * addresses from the pool ${SP}.
 */
struct dynamodb_request_queue *
dynamodb_request_queue_init(const char * key_id, const char * key_secret,
    const char * region, struct serverpool * SP)
{
	struct dynamodb_request_queue * Q;

	/* Allocate a request queue structure. */
	if ((Q = malloc(sizeof(struct dynamodb_request_queue))) == NULL)
		goto err0;

	/* Copy in strings. */
	if ((Q->key_id = strdup(key_id)) == NULL)
		goto err1;
	if ((Q->key_secret = strdup(key_secret)) == NULL)
		goto err2;
	if ((Q->region = strdup(region)) == NULL)
		goto err3;

	/* Record the server pool to draw IP addresses from. */
	Q->SP = SP;

	/*
	 * Initialize rate-limiting parameters.  The initial bucket capacity
	 * is set to 300 seconds of 50k capacity units per second; this
	 * allows an effectively unlimited burst until the first "capacity
	 * exceeded" warning is seen, after which bucket_cap is limited to
	 * 300 seconds of provisioned capacity.
	 */
	Q->mu_capperreq = 1.0;
	Q->bucket_cap = 300.0 * 50000.0;
	dynamodb_request_queue_setcapacity(Q, 0);

	/* Initialize request timeout statistics to conservative values. */
	Q->tmu = 1.0;
	Q->tmud = 0.25;

	/* We have no pending events. */
	Q->timer_cookie = NULL;
	Q->immediate_cookie = NULL;

	/* No requests yet. */
	if ((Q->reqs = ptrheap_init(compar, setreccookie, Q)) == NULL)
		goto err4;
	Q->reqnum = 0;
	Q->inflight = 0;

	/* No log file yet. */
	Q->logfile = NULL;

	/* Success! */
	return (Q);

err4:
	free(Q->region);
err3:
	insecure_memzero(Q->key_secret, strlen(Q->key_secret));
	free(Q->key_secret);
err2:
	free(Q->key_id);
err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * dynamodb_request_queue_log(Q, F):
 * Log all requests performed by the queue ${Q} to the log file ${F}.
 */
void
dynamodb_request_queue_log(struct dynamodb_request_queue * Q,
    struct logging_file * F)
{

	Q->logfile = F;
}

/**
 * dynamodb_request_queue_setcapacity(Q, capacity):
 * Set the capacity of the DyanamoDB request queue to ${capacity} capacity
 * units per second; use this value (along with ConsumedCapacity fields from
 * DynamoDB responses) to rate-limit requests after seeing a "Throughput
 * Exceeded" exception.  If passed a capacity of 0, the request rate will
 * not be limited.
 */
void
dynamodb_request_queue_setcapacity(struct dynamodb_request_queue * Q,
    int capacity)
{

	/* How long does it take for one capacity unit to arrive? */
	if (capacity > 0)
		Q->spercap = 1.0 / capacity;
	else
		Q->spercap = 0.0;

	/*
	 * Allow up to 5 seconds worth of requests to be in flight at once
	 * (in the event of request bursts), up to a maximum of 500 requests
	 * (to avoid having an unreasonable number of connections open at
	 * once -- with single-digit request latencies, this is >50k requests
	 * per second, so it's not likely to be a problem).
	 */
	if ((capacity > 0) && (capacity < 100))
		Q->maxburst_cap = capacity * 5.0;
	else
		Q->maxburst_cap = 500.0;
}

/**
 * dynamodb_request_queue(Q, prio, op, body, maxrlen, logstr, callback, cookie):
 * Using the DynamoDB request queue ${Q}, queue the DynamoDB request
 * contained in ${body} for the operation ${op}.  Read a response with a body
 * of up to ${maxrlen} bytes and invoke the callback as per dynamodb_request.
 * The strings ${op} and ${body} must remain valid until the callback is
 * invoked or the queue is flushed.  For accurate rate limiting, on tables
 * with "provisioned" capacity requests must elicit ConsumedCapacity fields
 * in their responses.
 * 
 * HTTP 5xx errors and HTTP 400 "Throughput Exceeded" errors will be
 * automatically retried; other errors are passed back.
 *
 * Requests will be served starting with the lowest ${prio}, breaking ties
 * according to the queue arrival time.
 * 
 * If dynamodb_request_queue_log has been called, ${logstr} will be included
 * when this request is logged.  (This could be used to identify the target
 * of the ${op} operation, for example.)
 */
int
dynamodb_request_queue(struct dynamodb_request_queue * Q, int prio,
    const char * op, const char * body, size_t maxrlen, const char * logstr,
    int (* callback)(void *, struct http_response *), void * cookie)
{
	struct request * R;

	/* Allocate and fill request structure. */
	if ((R = malloc(sizeof(struct request))) == NULL)
		goto err0;
	R->Q = Q;
	R->op = op;
	R->body = (const uint8_t *)body;
	R->bodylen = strlen(body);
	R->maxrlen = maxrlen;
	R->callback = callback;
	R->cookie = cookie;
	R->http_cookie = NULL;
	R->timeout_cookie = NULL;
	R->addrs[0] = NULL;
	R->addrs[1] = NULL;
	R->prio = prio;
	R->reqnum = Q->reqnum++;
	R->rc = 0;
	R->ntries = 0;

	/* Duplicate the additional logging data. */
	if (logstr != NULL) {
		if ((R->logstr = strdup(logstr)) == NULL)
			goto err1;
	} else {
		R->logstr = NULL;
	}

	/* Add the request to the queue. */
	if (ptrheap_add(Q->reqs, R))
		goto err2;

	/* Poke the request queue if necessary. */
	if (poke(Q))
		goto err3;

	/* Success! */
	return (0);

err3:
	ptrheap_delete(Q->reqs, R->rc);
err2:
	free(R->logstr);
err1:
	free(R);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dynamodb_request_queue_flush(Q):
 * Flush the DynamoDB request queue ${Q}.  Any queued requests will be
 * dropped; no callbacks will be performed.
 */
void
dynamodb_request_queue_flush(struct dynamodb_request_queue * Q)
{
	struct request * R;

	/* Pull requests off the queue until there are none left. */
	while ((R = ptrheap_getmin(Q->reqs)) != NULL) {
		/* Delete it from the queue. */
		ptrheap_deletemin(Q->reqs);

		/* Cancel any in-progress operation. */
		if (R->timeout_cookie != NULL)
			events_timer_cancel(R->timeout_cookie);
		if (R->http_cookie != NULL) {
			http_request_cancel(R->http_cookie);
			sock_addr_free(R->addrs[0]);
			Q->inflight--;
		}

		/* Free the request. */
		free(R->logstr);
		free(R);
	}
}

/**
 * dynamodb_request_queue_free(Q):
 * Free the DynamoDB request queue ${Q}.  Any queued requests will be
 * dropped; no callbacks will be performed.
 */
void
dynamodb_request_queue_free(struct dynamodb_request_queue * Q)
{

	/* Flush the queue. */
	dynamodb_request_queue_flush(Q);

	/* Stop the rate-limiting timer (if any). */
	if (Q->timer_cookie != NULL)
		events_timer_cancel(Q->timer_cookie);

	/* Cancel the immediate callback (if any). */
	if (Q->immediate_cookie != NULL)
		events_immediate_cancel(Q->immediate_cookie);

	/* Free the (now empty) request queue. */
	ptrheap_free(Q->reqs);

	/* Free string allocated by strdup. */
	free(Q->region);

	/* Free the AWS keys. */
	insecure_memzero(Q->key_secret, strlen(Q->key_secret));
	free(Q->key_secret);
	free(Q->key_id);

	/* Free the request queue structure. */
	free(Q);
}

#ifndef _DYNAMODB_REQUEST_QUEUE_H_
#define _DYNAMODB_REQUEST_QUEUE_H_

#include <stddef.h>

/* Opaque types. */
struct dynamodb_request_queue;
struct http_response;
struct logging_file;
struct serverpool;

/**
 * dynamodb_request_queue_init(key_id, key_secret, region, SP):
 * Create a DynamoDB request queue using AWS key id ${key_id} and secret key
 * ${key_secret} to make requests to DynamoDB in ${region}.  Obtain target
 * addresses from the pool ${SP}.
 */
struct dynamodb_request_queue * dynamodb_request_queue_init(const char *,
    const char *, const char *, struct serverpool *);

/**
 * dynamodb_request_queue_log(Q, F):
 * Log all requests performed by the queue ${Q} to the log file ${F}.
 */
void dynamodb_request_queue_log(struct dynamodb_request_queue *,
    struct logging_file *);

/**
 * dynamodb_request_queue_setcapacity(Q, capacity):
 * Set the capacity of the DyanamoDB request queue to ${capacity} capacity
 * units per second; use this value (along with ConsumedCapacity fields from
 * DynamoDB responses) to rate-limit requests after seeing a "Throughput
 * Exceeded" exception.  If passed a capacity of 0, the request rate will
 * not be limited.
 */
void dynamodb_request_queue_setcapacity(struct dynamodb_request_queue *, long);

/**
 * dynamodb_request_queue(Q, prio, op, body, maxrlen, logstr, callback, cookie):
 * Using the DynamoDB request queue ${Q}, queue the DynamoDB request
 * contained in ${body} for the operation ${op}.  Read a response with a body
 * of up to ${maxrlen} bytes and invoke the callback with the provided cookie,
 * the HTTP response, and (for HTTP 400 responses) the DynamoDB error string.
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
 * If dynamodb_request_queue_log() has been called, ${logstr} will be included
 * when this request is logged.  (This could be used to identify the target
 * of the ${op} operation, for example.)
 */
int dynamodb_request_queue(struct dynamodb_request_queue *, int, const char *,
    const char *, size_t, const char *,
    int (*)(void *, struct http_response *, const char *), void *);

/**
 * dynamodb_request_queue_flush(Q):
 * Flush the DynamoDB request queue ${Q}.  Any queued requests will be
 * dropped; no callbacks will be performed.
 */
void dynamodb_request_queue_flush(struct dynamodb_request_queue *);

/**
 * dynamodb_request_queue_free(Q):
 * Free the DynamoDB request queue ${Q}.  Any queued requests will be
 * dropped; no callbacks will be performed.
 */
void dynamodb_request_queue_free(struct dynamodb_request_queue *);

#endif /* !_DYNAMODB_REQUEST_QUEUE_H_ */

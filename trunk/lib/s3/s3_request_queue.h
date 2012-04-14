#ifndef _S3_REQUEST_QUEUE_H_
#define _S3_REQUEST_QUEUE_H_

/* Opaque types. */
struct http_response;
struct logging_file;
struct sock_addr;
struct s3_request;
struct s3_request_queue;

/**
 * s3_request_queue_init(key_id, key_secret, conns):
 * Create an S3 request queue using the AWS Key ID ${key_id} and the Secret
 * Access Key ${key_secret} to perform up to ${conns} simultaneous requests.
 */
struct s3_request_queue * s3_request_queue_init(const char *, const char *,
    size_t);

/**
 * s3_request_queue_log(Q, F):
 * Log all S3 requests performed by the queue ${Q} to the log file ${F}.
 */
void s3_request_queue_log(struct s3_request_queue *, struct logging_file *);

/**
 * s3_request_queue_addaddr(Q, addr, ttl):
 * Add the address ${addr} to the S3 request queue ${Q}, valid for the next
 * ${ttl} seconds.  The address ${addr} is copied and does not need to remain
 * valid after the call returns.
 */
int s3_request_queue_addaddr(struct s3_request_queue *,
    const struct sock_addr *, int);

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
int s3_request_queue(struct s3_request_queue *, struct s3_request *, size_t,
    int (*)(void *, struct http_response *), void *);

/**
 * s3_request_queue_flush(Q):
 * Flush the S3 request queue ${Q}.  Any queued requests will be dropped; no
 * callbacks will be performed.
 */
void s3_request_queue_flush(struct s3_request_queue *);

/**
 * s3_request_queue_free(Q):
 * Free the S3 request queue ${Q}.  Any queued requests will be dropped; no
 * callbacks will be performed.
 */
void s3_request_queue_free(struct s3_request_queue *);

#endif /* !_S3_REQUEST_QUEUE_H_ */

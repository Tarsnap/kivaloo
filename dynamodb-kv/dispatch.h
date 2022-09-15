#ifndef DISPATCH_H_
#define DISPATCH_H_

#include <stdint.h>

/* Opaque type. */
struct dynamodb_request_queue;

/**
 * dispatch_accept(QW, QR, table, s):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for sending requests to the DynamoDB queues ${QW} (writes/deletes)
 * and ${QR} (reads) for operations on table ${table}.
 */
struct dispatch_state * dispatch_accept(struct dynamodb_request_queue *,
    struct dynamodb_request_queue *, const char *, int);

/**
 * dispatch_alive(D):
 * Return non-zero iff the current connection being handled by the dispatcher
 * ${D} is still alive (if it is reading requests, has requests queued, is
 * processing requests, has responses queued up to be sent back, et cetera).
 */
int dispatch_alive(struct dispatch_state *);

/**
 * dispatch_done(D):
 * Clean up the dispatch state ${D}.  The function dispatch_alive(${D}) must
 * have previously returned zero.
 */
int dispatch_done(struct dispatch_state *);

#endif /* !DISPATCH_H_ */

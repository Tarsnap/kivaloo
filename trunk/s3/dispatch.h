#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include <stdint.h>

/* Opaque type. */
struct s3_request_queue;

/**
 * dispatch_accept(Q, s):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for the S3 request queue ${Q}.
 */
struct dispatch_state * dispatch_accept(struct s3_request_queue *, int);

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

#endif /* !_DISPATCH_H_ */

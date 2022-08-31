#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct dispatch_state;
struct storage_state;

/**
 * dispatch_init(S, blocklen, nreaders):
 * Initialize a dispatcher to manage requests to storage state ${S} with
 * block size ${blocklen}, using ${nreaders} read threads.
 */
struct dispatch_state * dispatch_init(struct storage_state *, size_t, size_t);

/**
 * dispatch_accept(D, s):
 * Accept a connection from the listening socket ${s} and perform all
 * associated initialization in the dispatcher ${D}.
 */
int dispatch_accept(struct dispatch_state *, int);

/**
 * dispatch_alive(D):
 * Return non-zero iff the current connection being handled by the dispatcher
 * ${D} is still alive (if it is reading requests, has requests queued, is
 * processing requests, has responses queued up to be sent back, et cetera).
 */
int dispatch_alive(struct dispatch_state *);

/**
 * dispatch_close(D):
 * Clean up and close the current connection being handled by the dispatcher
 * ${D}.  The function dispatch_alive() must have previously returned a
 * non-zero value.
 */
int dispatch_close(struct dispatch_state *);

/**
 * dispatch_done(D):
 * Clean up and free the dispatcher ${D}.
 */
int dispatch_done(struct dispatch_state *);

#endif /* !_DISPATCH_H_ */

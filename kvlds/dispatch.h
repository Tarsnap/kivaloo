#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include <stddef.h>

/* Opaque types. */
struct btree;
struct dispatch_state;
struct netbuf_write;
struct proto_kvlds_request;

/**
 * dispatch_accept(s, T, kmax, vmax, w, g):
 * Accept a connection from the listening socket ${s} and return a dispatch
 * state for the B+Tree ${T}.  Keys will be at most ${kmax} bytes; values
 * will be at most ${vmax} bytes; up to ${w} seconds should be spent waiting
 * for more requests before performing a group commit, unless ${g} requests
 * are pending.
 */
struct dispatch_state * dispatch_accept(int, struct btree *, size_t, size_t,
    double, size_t);

/**
 * dispatch_alive(D):
 * Return non-zero iff the dispatch state ${D} is still alive (if it is
 * waiting for a connection to arrive, is reading requests, or has requests
 * in progress).
 */
int dispatch_alive(struct dispatch_state *);

/**
 * dispatch_done(D):
 * Clean up the dispatch state ${D}.  The function dispatch_alive(${D}) must
 * have previously returned zero.
 */
int dispatch_done(struct dispatch_state *);

/**
 * dispatch_nmr_launch(T, R, WQ, callback_done, cookie_done):
 * Perform non-modifying request ${R} on the B+Tree ${T}; write a response
 * packet to the write queue ${WQ}; and free the requests.  Invoke the
 * callback ${callback_done}(${cookie_done}) after the request is processed.
 */
int dispatch_nmr_launch(struct btree *, struct proto_kvlds_request *,
    struct netbuf_write *, int (*)(void *), void *);

/**
 * dispatch_mr_launch(T, reqs, nreqs, WQ, callback_done, cookie):
 * Perform the ${nreqs} modifying requests ${reqs[0]} ... ${reqs[nreqs - 1]}
 * on the B+Tree ${T}; write response packets to the write queue ${WQ}; and
 * free the requests and request array.  Invoke the callback
 * ${callback_done}(${cookie}) after the requests have been serviced.
 */
int dispatch_mr_launch(struct btree *, struct proto_kvlds_request **, size_t,
    struct netbuf_write *, int (*)(void *), void *);

#endif /* !_DISPATCH_H_ */

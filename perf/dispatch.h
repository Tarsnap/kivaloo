#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include <stddef.h>

/* Opaque types. */
struct dispatch_state;
struct perfstats;
struct wire_requestqueue;

/**
 * dispatch_accept(s, Q, P):
 * Initialize a dispatcher to accept a connection from the listening socket
 * ${s} and shuttle request/respones to/from the request queue ${Q}, recording
 * performance for each request via ${P}.
 */
struct dispatch_state * dispatch_accept(int, struct wire_requestqueue *,
    struct perfstats *);

/**
 * dispatch_alive(dstate):
 * Return non-zero if the dispatcher with state ${dstate} is still alive.
 */
int dispatch_alive(struct dispatch_state *);

/**
 * dispatch_done(dstate):
 * Clean up the dispatcher state ${dstate}.
 */
void dispatch_done(struct dispatch_state *);

#endif /* !_DISPATCH_H_ */

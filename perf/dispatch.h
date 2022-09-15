#ifndef DISPATCH_H_
#define DISPATCH_H_

#include <stddef.h>

/* Opaque types. */
struct dispatch_state;
struct perfstats;
struct wire_requestqueue;

/**
 * dispatch_accept(s, Q, P):
 * Initialize a dispatcher to accept a connection from the listening socket
 * ${s} and shuttle request/responses to/from the request queue ${Q}, recording
 * performance for each request via ${P}.
 */
struct dispatch_state * dispatch_accept(int, struct wire_requestqueue *,
    struct perfstats *);

/**
 * dispatch_alive(D):
 * Return non-zero if the dispatcher with state ${D} is still alive.
 */
int dispatch_alive(struct dispatch_state *);

/**
 * dispatch_done(D):
 * Clean up the dispatcher state ${D}.
 */
void dispatch_done(struct dispatch_state *);

#endif /* !DISPATCH_H_ */

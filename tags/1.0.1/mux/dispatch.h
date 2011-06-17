#ifndef _DISPATCH_H_
#define _DISPATCH_H_

/* Opaque types. */
struct dispatch_state;
struct wire_requestqueue;

/**
 * dispatch_init(socks, nsocks, Q, maxconn):
 * Initialize a dispatcher to accept connections from the listening sockets
 * ${socks}[0 .. ${nsocks} - 1] (but no more than ${maxconn} at once) and
 * shuttle requests/responses to/from the request queue ${Q}.
 */
struct dispatch_state *
dispatch_init(const int *, size_t, struct wire_requestqueue *, size_t);

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

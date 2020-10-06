#ifndef _KIVALOO_H_
#define _KIVALOO_H_

/* Opaque types. */
struct wire_requestqueue;

/**
 * kivaloo_open(addr, Q):
 * Resolve the socket address ${addr}, connect to it, and create a wire
 * request queue.  Return the request queue via ${Q}; and return a cookie
 * which can be passed to kivaloo_close() to shut down the queue and release
 * resources.
 */
void * kivaloo_open(const char *, struct wire_requestqueue **);

/**
 * kivaloo_close(cookie):
 * Destroy and free the wire request queue, close the socket and free memory
 * allocated by the kivaloo_open() which returned ${cookie}.
 */
void kivaloo_close(void *);

#endif /* !_KIVALOO_H_ */

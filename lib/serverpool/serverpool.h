#ifndef _SERVERPOOL_H_
#define _SERVERPOOL_H_

#include <time.h>

struct sock_addr;

/**
 * serverpool_create(target, freq, ttl):
 * Fork off a process to perform DNS lookups for ${target}, sleeping ${freq}
 * seconds between lookups.  Keep returned addresses for ${ttl} seconds, and
 * always keep at least one address (even if no addresses have been returned
 * in the past ${ttl} seconds).  Return a cookie which can be passed to
 * serverpool_pick().
 */
struct serverpool * serverpool_create(const char *, int, time_t);

/**
 * serverpool_pick(P):
 * Return an address randomly selected from the addresses in the pool ${P}.
 * The callers is responsible for freeing the address.
 */
struct sock_addr * serverpool_pick(struct serverpool *);

/**
 * serverpool_free(P):
 * Stop performing DNS lookups and free the server pool ${P}.
 */
void serverpool_free(struct serverpool *);

#endif /* !_SERVERPOOL_H_ */

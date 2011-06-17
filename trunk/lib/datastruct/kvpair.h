#ifndef _KVPAIR_H_
#define _KVPAIR_H_

#include "kvldskey.h"

/* Key-value pair structure. */
struct kvpair {
	struct kvldskey * k;
	struct kvldskey * v;
};
struct kvpair_const {
	const struct kvldskey * k;
	const struct kvldskey * v;
};

/**
 * kvpair_cmp(cookie, x, y):
 * Compare the keys in the kvpairs ${x} and ${y}.  The keys share a common
 * prefix of *(size_t *)(${cookie}) bytes.
 */
int kvpair_cmp(void *, const void *, const void *);

/**
 * kvpair_sort(pairs, npairs, mlen):
 * Sort the ${npairs} key-value pairs at ${pairs}.  The keys all share a
 * common prefix of ${mlen} bytes.  This function is not thread safe.
 */
void kvpair_sort(struct kvpair *, size_t, size_t);

#endif /* !_KVPAIR_H_ */

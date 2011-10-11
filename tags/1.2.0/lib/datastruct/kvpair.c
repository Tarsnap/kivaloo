#include <stdlib.h>

#include "kvldskey.h"

#include "kvpair.h"

/**
 * kvpair_cmp(cookie, x, y):
 * Compare the keys in the kvpairs ${x} and ${y}.  The keys share a common
 * prefix of *(size_t *)(${cookie}) bytes.
 */
int
kvpair_cmp(void * cookie, const void * _x, const void * _y)
{
	const struct kvpair * x = _x;
	const struct kvpair * y = _y;
	size_t * mlen = cookie;

	return (kvldskey_cmp2(x->k, y->k, *mlen));
}

/*
 * Ugly hack warning: POSIX doesn't provide qsort_r, so we're using qsort and
 * storing the cookie (in this case, the keys-match-up-to length) as a static
 * global variable.  This is not thread safe (the reason for which qsort_r
 * exists) and thus kvpair_sort is not thread safe.
 */
static size_t cookie_mlen;
static int
compar(const void * _x, const void * _y)
{
	const struct kvpair * x = _x;
	const struct kvpair * y = _y;

	return (kvldskey_cmp2(x->k, y->k, cookie_mlen));
}


/**
 * kvpair_sort(pairs, npairs, mlen):
 * Sort the ${npairs} key-value pairs at ${pairs}.  The keys all share a
 * common prefix of ${mlen} bytes.  This function is not thread safe.
 */
void
kvpair_sort(struct kvpair * pairs, size_t npairs, size_t mlen)
{

	/* See above comments about cookie_mlen and non-threadsafety. */
	cookie_mlen = mlen;

	/* Do the sort. */
	qsort(pairs, npairs, sizeof(struct kvpair), compar);
}

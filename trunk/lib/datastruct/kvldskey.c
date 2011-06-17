#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kvldskey.h"

/**
 * kvldskey_create(buf, buflen):
 * Create and return a key.
 */
struct kvldskey *
kvldskey_create(const uint8_t * buf, size_t len)
{
	struct kvldskey * K;

	/* Allocate structure. */
	if ((K = malloc(sizeof(struct kvldskey) + len)) == NULL)
		goto err0;

	/* Copy data. */
	K->len = len;
	memcpy(K->buf, buf, len);

	/* Success! */
	return (K);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * kvldskey_unserialize(K, buf, buflen):
 * Deserialize ${K} from the ${buflen}-byte buffer ${buf} and return the
 * number of bytes consumed.  Return 0 on error or if the buffer does not
 * contain a valid serialization; return with errno equal to zero iff the
 * serialization is invalid.
 */
size_t
kvldskey_unserialize(struct kvldskey ** K, const uint8_t * buf, size_t buflen)
{
	size_t len;

	/* Length. */
	if (buflen < 1)
		goto err1;
	if (buflen < (len = (size_t)(buf[0]) + 1))
		goto err1;

	/* Allocate structure and copy data. */
	if ((*K = malloc(len)) == NULL)
		goto err0;
	memcpy(*K, buf, len);

	/* Success! */
	return (len);

err1:
	errno = 0;
err0:
	/* Failure! */
	return (0);
}

/**
 * kvldskey_cmp(x, y):
 * Returns < 0, 0, or > 0, depending on whether ${x} is lexicographically
 * less than, equal to, or greater than ${y}.
 */
int
kvldskey_cmp(const struct kvldskey * x, const struct kvldskey * y)
{

	return (kvldskey_cmp2(x, y, 0));
}

/**
 * kvldskey_cmp2(x, y, mlen):
 * Returns < 0, 0, or > 0, depending on whether ${x} is lexicographically
 * less than, equal to, or greater than ${y}.  The strings are known to match
 * up to ${mlen} bytes.
 */
int
kvldskey_cmp2(const struct kvldskey * x, const struct kvldskey * y,
    size_t mlen)
{
	size_t minlen = (x->len < y->len) ? x->len : y->len;
	int rc;

	for (; mlen < minlen; mlen++) {
		if ((rc = x->buf[mlen] - y->buf[mlen]) != 0)
			return (rc);
	}
	return (x->len - y->len);
}

/**
 * kvldskey_mlen(x, y):
 * For keys ${x} < ${y}, return the length of the matching prefix.
 */
size_t
kvldskey_mlen(const struct kvldskey * x, const struct kvldskey * y)
{
	size_t mlen;

	/* So far we're aware of zero matching bytes. */
	mlen = 0;

	/* Consider positions one by one. */
	do {
		/* If ${x} has length mlen, it can't match any further. */
		if (x->len == mlen)
			break;

		/*
		 * ${y} is greater than ${x}, which is greater than their
		 * shared ${mlen}-byte prefix; so ${y} must also have length
		 * greater than ${mlen}.  Do the two keys match in their
		 * next position?
		 */
		if (x->buf[mlen] != y->buf[mlen])
			break;

		/* The keys match one byte further. */
		mlen += 1;
	} while (1);

	/* Return the matching length. */
	return (mlen);
}

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
	K->refcnt = 1;

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
 * kvldskey_serialize(K, buf):
 * Serialize ${K} into the buffer ${buf}.
 */
void
kvldskey_serialize(const struct kvldskey * K, uint8_t * buf)
{
	uint8_t * p = buf;

	/* Key length. */
	*p = K->len;
	p += 1;

	/* Key data. */
	memcpy(p, K->buf, K->len);
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
	const uint8_t * p = buf;
	size_t len;

	/* Length. */
	if (buflen < 1)
		goto err1;
	len = p[0];
	p += 1;
	buflen -= 1;

	/* Allocate structure. */
	if ((*K = malloc(sizeof(struct kvldskey) + len)) == NULL)
		goto err0;
	(*K)->refcnt = 1;
	(*K)->len = len;

	/* Copy key data. */
	if (buflen < len)
		goto err2;
	memcpy((*K)->buf, p, len);
	p += len;

	/* Success! */
	return (p - buf);

err2:
	free(*K);
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
	int rc;
	size_t xlen = x->len;
	size_t ylen = y->len;
	const uint8_t * xbuf = x->buf;
	const uint8_t * ybuf = y->buf;

	do {
		if ((xlen | ylen) == 0)
			return (0);
		else if (xlen == 0)
			return (-1);
		else if (ylen == 0)
			return (1);

		if ((rc = *xbuf - *ybuf) != 0)
			return (rc);

		xlen--; xbuf++;
		ylen--; ybuf++;
	} while (1);
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
	int rc;
	size_t xlen = x->len - mlen;
	size_t ylen = y->len - mlen;
	const uint8_t * xbuf = x->buf + mlen;
	const uint8_t * ybuf = y->buf + mlen;

	do {
		if ((xlen | ylen) == 0)
			return (0);
		else if (xlen == 0)
			return (-1);
		else if (ylen == 0)
			return (1);

		if ((rc = *xbuf - *ybuf) != 0)
			return (rc);

		xlen--; xbuf++;
		ylen--; ybuf++;
	} while (1);
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

/**
 * kvldskey_free(K):
 * Decrement the reference count of ${K} and free ${K} if it becomes zero.
 */
void
kvldskey_free(struct kvldskey * K)
{

	/* Be compatible with free(NULL). */
	if (K == NULL)
		return;

	/* Decrement reference count. */
	K->refcnt -= 1;

	/* Free key structure if it has no references. */
	if (K->refcnt == 0)
		free(K);
}

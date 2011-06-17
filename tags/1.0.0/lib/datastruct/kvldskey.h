#ifndef _KVLDSKEY_H_
#define _KVLDSKEY_H_

#include <stddef.h>
#include <stdint.h>

struct kvldskey {
	uint8_t refcnt;
	uint8_t len;
	uint8_t buf[];
};

/**
 * kvldskey_create(buf, buflen):
 * Create and return a key.
 */
struct kvldskey * kvldskey_create(const uint8_t *, size_t);

/**
 * kvldskey_serial_size(K):
 * Return the size in bytes of the serialization of ${K}.
 */
#define kvldskey_serial_size(K) ((K)->len + 1)

/**
 * kvldskey_serialize(K, buf):
 * Serialize ${K} into the buffer ${buf}.
 */
void kvldskey_serialize(const struct kvldskey *, uint8_t *);

/**
 * kvldskey_unserialize(K, buf, buflen):
 * Deserialize ${K} from the ${buflen}-byte buffer ${buf} and return the
 * number of bytes consumed.  Return 0 on error or if the buffer does not
 * contain a valid serialization; return with errno equal to zero iff the
 * serialization is invalid.
 */
size_t kvldskey_unserialize(struct kvldskey **, const uint8_t *, size_t);

/**
 * kvldskey_ref(K):
 * Add a reference to the key ${K}.
 */
#define kvldskey_ref(K)	do {		\
	assert((K)->refcnt < 255);	\
	(K)->refcnt += 1;		\
} while (0)

/**
 * kvldskey_cmp(x, y):
 * Returns < 0, 0, or > 0, depending on whether ${x} is lexicographically
 * less than, equal to, or greater than ${y}.
 */
int kvldskey_cmp(const struct kvldskey *, const struct kvldskey *);

/**
 * kvldskey_cmp2(x, y, mlen):
 * Returns < 0, 0, or > 0, depending on whether ${x} is lexicographically
 * less than, equal to, or greater than ${y}.  The strings are known to match
 * up to ${mlen} bytes.
 */
int kvldskey_cmp2(const struct kvldskey *, const struct kvldskey *, size_t);

/**
 * kvldskey_mlen(x, y):
 * For keys ${x} < ${y}, return the length of the matching prefix.
 */
size_t kvldskey_mlen(const struct kvldskey *, const struct kvldskey *);

/**
 * kvldskey_sep(x, y):
 * For keys ${x} < ${y}, return a new key ${S} such that ${x} < ${S} <= ${y}.
 */
static inline struct kvldskey *
kvldskey_sep(const struct kvldskey * x, const struct kvldskey * y)
{

	return (kvldskey_create(y->buf, kvldskey_mlen(x, y) + 1));
}

/**
 * kvldskey_free(K):
 * Decrement the reference count of ${K} and free ${K} if it becomes zero.
 */
void kvldskey_free(struct kvldskey *);

#endif /* !_KVLDSKEY_H_ */

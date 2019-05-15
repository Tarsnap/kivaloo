#ifndef _KVLDSKEY_H_
#define _KVLDSKEY_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "ctassert.h"

struct kvldskey {
	uint8_t len;
	uint8_t buf[];
};

/*
 * Make sure that struct kvldskey is packed.  It's hard to imagine a compiler
 * which would add padding between len and buf, but it's technically allowed.
 */
CTASSERT(sizeof(struct kvldskey) == 1);

/**
 * kvldskey_create(buf, buflen):
 * Create and return a key.
 */
struct kvldskey * kvldskey_create(const uint8_t *, size_t);

/**
 * kvldskey_serial_size(K):
 * Return the size in bytes of the serialization of ${K}.
 */
#define kvldskey_serial_size(K) ((size_t)((K)->len) + 1)

/**
 * kvldskey_serialize(K, buf):
 * Serialize ${K} into the buffer ${buf}.
 */
#define kvldskey_serialize(K, buf) memcpy((buf), (K), (K)->len + 1)

/**
 * kvldskey_unserialize(K, buf, buflen):
 * Deserialize ${K} from the ${buflen}-byte buffer ${buf} and return the
 * number of bytes consumed.  Return 0 on error or if the buffer does not
 * contain a valid serialization; return with errno equal to zero iff the
 * serialization is invalid.
 */
size_t kvldskey_unserialize(struct kvldskey **, const uint8_t *, size_t);

/**
 * kvldskey_dup(K):
 * Duplicate the key ${K}.
 */
#define kvldskey_dup(K) kvldskey_create((K)->buf, (K)->len)

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
 * kvldskey_free(K):
 * Free the key ${K}.
 */
#define kvldskey_free(K) free(K)

#endif /* !_KVLDSKEY_H_ */

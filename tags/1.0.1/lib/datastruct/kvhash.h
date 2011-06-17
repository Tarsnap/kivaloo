#ifndef _KVHASH_H_
#define _KVHASH_H_

#include <stdint.h>

#include "kvldskey.h"
#include "kvpair.h"

/**
 * A kvhash structure is a hash table containing keys which each has either
 * a value or NULL associated with it.  New keys can be added to a kvhash,
 * but keys cannot be deleted.  A kvhash cannot contain more than 2^30 keys.
 */
/**
 * Invariants:
 * 1. (pairs[i].k != NULL) ==> (hashes[i] == hash(pairs[i].k)).
 * 2. (pairs[i].k == NULL) ==> (pairs[i].v == NULL).
 */
struct kvhash {
	struct kvpair * pairs;
	uint32_t * hashes;
	size_t nkeys;
	size_t nslots;
};

/**
 * kvhash_init(void):
 * Return an empty kvhash.
 */
struct kvhash * kvhash_init(void);

/**
 * kvhash_search(H, k):
 * Search for the key ${k} in the kvhash ${H}.  Return a pointer to the
 * kvpair structure where the key appears or would appear if inserted.  Write
 * the hash value into the corresponding location in the hashes array.
 */
struct kvpair * kvhash_search(struct kvhash *, const struct kvldskey *);

/**
 * kvhash_postadd(H):
 * Record that key-value pair has been added to the kvhash ${H}.  Rehash
 * (expand) the table if necessary.
 */
int kvhash_postadd(struct kvhash *);

/**
 * kvhash_free(H):
 * Free the kvhash ${H}.  Do not free the keys or values it holds.
 */
void kvhash_free(struct kvhash *);

#endif /* !_KVHASH_H_ */

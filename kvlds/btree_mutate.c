#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "imalloc.h"
#include "kvhash.h"
#include "kvldskey.h"
#include "kvpair.h"

#include "btree_find.h"
#include "node.h"

#include "btree_mutate.h"

/**
 * btree_mutate_mutable(N):
 * Make the leaf node ${N} mutable.
 */
int
btree_mutate_mutable(struct node * N)
{

	/* Sanity check. */
	assert(N->type == NODE_TYPE_LEAF);
	assert(N->state == NODE_STATE_DIRTY);
	assert(N->pagesize == (uint32_t)(-1));
	assert(N->v.H == NULL);

	/* Create a hash table for short-term key-value storage. */
	if ((N->v.H = kvhash_init()) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_mutate_find(N, k):
 * Search for the key ${k} in the mutable leaf node ${N}.  Return the kvpair
 * in which it belongs.
 */
struct kvpair_const *
btree_mutate_find(struct node * N, const struct kvldskey *k)
{
	struct kvpair_const * kv;

	/* Look for the key in the sorted key vector. */
	if ((kv = btree_find_kvpair(N, k)) != NULL)
		return (kv);

	/* Look for the key in the hash table. */
	return (kvhash_search(N->v.H, k));
}

/**
 * btree_mutate_add(N, pos, k, v):
 * Add the key-value pair ${k}/${v} to the mutable leaf node ${N} in position
 * ${pos}, and update structures.
 */
int
btree_mutate_add(struct node * N, struct kvpair_const * pos,
    const struct kvldskey * k, const struct kvldskey * v)
{
	size_t mlen;

	/* Update the all-keys-present-match-up-to value. */
	if (N->nkeys) {
		mlen = kvldskey_mlen(k, N->u.pairs[0].k);
		if (mlen < N->mlen_n)
			N->mlen_n = (uint8_t)mlen;
	} else {
		N->mlen_n = 0;
	}

	/* Record the pair. */
	pos->k = k;
	pos->v = v;

	/* Grow the hash table if necessary. */
	return (kvhash_postadd(N->v.H));
}

/**
 * btree_mutate_immutable(N):
 * Mutations on the leaf node ${N} are done (for now).
 */
int
btree_mutate_immutable(struct node * N)
{
	size_t nkeys_list, nkeys_hash;
	struct kvpair_const * new_pairs;
	size_t new_nkeys;
	size_t i, j, k;

	/* Sanity check. */
	assert(N->type == NODE_TYPE_LEAF);
	assert(N->state == NODE_STATE_DIRTY);
	assert(N->pagesize == (uint32_t)(-1));

	/* Count keys with non-NULL values in the sorted list. */
	for (nkeys_list = i = 0; i < N->nkeys; i++)
		if (N->u.pairs[i].v != NULL)
			nkeys_list += 1;

	/* Count keys with non-NULL values in the hash table. */
	for (nkeys_hash = i = 0; i < N->v.H->nslots; i++)
		if (N->v.H->pairs[i].v != NULL)
			nkeys_hash += 1;

	/* Allocate new array of key-value pairs. */
	new_nkeys = nkeys_list + nkeys_hash;
	if (IMALLOC(new_pairs, new_nkeys, struct kvpair_const))
		goto err0;

	/* Copy pairs from the hash table to the end of the array. */
	for (j = nkeys_list, i = 0; i < N->v.H->nslots; i++) {
		if (N->v.H->pairs[i].v != NULL) {
			new_pairs[j].k = N->v.H->pairs[i].k;
			new_pairs[j].v = N->v.H->pairs[i].v;
			j++;
		}
	}

	/* Sort the pairs from the hash table in-place. */
	kvpair_sort((struct kvpair *)&new_pairs[nkeys_list],
	    nkeys_hash, N->mlen_n);

	/* Merge pairs from the sorted list into the new array. */
	for (j = i = 0, k = nkeys_list; i < N->nkeys; i++) {
		/* Skip keys for which the value was deleted. */
		if (N->u.pairs[i].v == NULL)
			continue;

		/* Move hashed pairs with smaller keys than this one. */
		while ((k < new_nkeys) && (kvldskey_cmp2(N->u.pairs[i].k,
		    new_pairs[k].k, N->mlen_n) > 0)) {
			new_pairs[j].k = new_pairs[k].k;
			new_pairs[j].v = new_pairs[k].v;
			j++;
			k++;
		}

		/* Copy in the pair. */
		new_pairs[j].k = N->u.pairs[i].k;
		new_pairs[j].v = N->u.pairs[i].v;
		j++;
	}

	/* Free old array of key-value pairs. */
	free(N->u.pairs);

	/* Free the hash table. */
	kvhash_free(N->v.H);

	/* Update node. */
	N->u.pairs = new_pairs;
	N->nkeys = new_nkeys;
	N->v.H = NULL;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

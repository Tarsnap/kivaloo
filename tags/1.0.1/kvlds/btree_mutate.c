#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "kvhash.h"
#include "kvldskey.h"
#include "kvpair.h"
#include "imalloc.h"

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
struct kvpair *
btree_mutate_find(struct node * N, struct kvldskey *k)
{
	struct kvpair * kv;

	/* Look for the key in the sorted key vector. */
	if ((kv = btree_find_kvpair(N, k)) != NULL)
		return (kv);

	/* Look for the key in the hash table. */
	return (kvhash_search(N->v.H, k));
}

/**
 * btree_mutate_immutable(N):
 * Mutations on the leaf node ${N} are done (for now).
 */
int
btree_mutate_immutable(struct node * N)
{
	size_t nkeys_list, nkeys_hash;
	struct kvpair * new_pairs;
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
	if (IMALLOC(new_pairs, new_nkeys, struct kvpair))
		goto err0;

	/* Copy pairs from the hash table to the end of the array. */
	for (j = nkeys_list, i = 0; i < N->v.H->nslots; i++) {
		if (N->v.H->pairs[i].v != NULL) {
			new_pairs[j].k = N->v.H->pairs[i].k;
			new_pairs[j].v = N->v.H->pairs[i].v;
			j++;
		} else {
			kvldskey_free(N->v.H->pairs[i].k);
		}
	}

	/* Sort the pairs from the hash table in-place. */
	kvpair_sort(&new_pairs[nkeys_list], nkeys_hash, N->mlen);

	/* Merge pairs from the sorted list into the new array. */
	for (j = i = 0, k = nkeys_list; i < N->nkeys; i++) {
		/* If this value was deleted, free the key and move on. */
		if (N->u.pairs[i].v == NULL) {
			kvldskey_free(N->u.pairs[i].k);
			continue;
		}

		/* Move hashed pairs with smaller keys than this one. */
		while ((k < new_nkeys) && (kvldskey_cmp2(N->u.pairs[i].k,
		    new_pairs[k].k, N->mlen) > 0)) {
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

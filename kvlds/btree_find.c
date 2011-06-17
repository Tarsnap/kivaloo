#include <assert.h>
#include <stdlib.h>

#include "events.h"
#include "kvpair.h"
#include "kvldskey.h"
#include "mpool.h"

#include "btree.h"
#include "btree_node.h"
#include "node.h"

#include "btree_find.h"

/* Leaf-finding state. */
struct findleaf_cookie {
	int (*callback)(void *, struct node *);
	int (*callback_range)(void *, struct node *, struct kvldskey *);
	void * cookie;
	struct btree * T;
	struct node * N;
	const struct kvldskey * k;
	int h;
	struct kvldskey * e;
};

MPOOL(findleaf, struct findleaf_cookie, 4096);

/**
 * btree_find_kvpair(N, k):
 * Search for the key ${k} in the B+Tree node ${N}.  Return a pointer to the
 * key-value pair, or NULL if the key is not present.
 */
struct kvpair *
btree_find_kvpair(struct node * N, const struct kvldskey * k)
{
	size_t min, max, mid;
	int rc;

	/* We must be in a leaf node. */
	assert(N->type == NODE_TYPE_LEAF);

	/*
	 * This key could be anywhere from position 0 (less than key #0) up
	 * to position N->nkeys (greater than key #(nkeys - 1)).
	 */
	min = 0;
	max = N->nkeys;

	/* Keep looking until we figure out where it belongs. */
	while (min != max) {
		/* Compare to the midpoint. */
		mid = min + (max - min) / 2;
		rc = kvldskey_cmp2(k, N->u.pairs[mid].k, N->mlen);

		/* Adjust endpoints. */
		if (rc < 0) {
			/* The key is less than key #mid. */
			max = mid;
		} else if (rc > 0) {
			/* The key is greater than key #mid. */
			min = mid + 1;
		} else {
			/* Found it! */
			return (&N->u.pairs[mid]);
		}
	};

	/* We didn't find it. */
	return (NULL);
}

/**
 * btree_find_child(N, k):
 * Search for the key ${k} in the B+Tree parent node ${N}.  Return the
 * number of the child responsible for the key ${key}.
 */
size_t
btree_find_child(struct node * N, const struct kvldskey * k)
{
	size_t min, max, mid;
	int rc;

	/* We must be in a parent node. */
	assert(N->type == NODE_TYPE_PARENT);

	/* This key could belong anywhere from child 0 up to N->nkeys. */
	min = 0;
	max = N->nkeys;

	/* Keep looking until we figure out where it belongs. */
	while (min != max) {
		/* Compare to the midpoint. */
		mid = min + (max - min) / 2;
		rc = kvldskey_cmp2(k, N->u.keys[mid], N->mlen);

		/* Adjust endpoints. */
		if (rc < 0) {
			/* The key is less than key #mid. */
			max = mid;
		} else if (rc > 0) {
			/* The key is greater than or equal to key #mid. */
			min = mid + 1;
		} else {
			/* Found it! */
			return (mid + 1);
		}
	};

	/* This must be it. */
	return (min);
}

/* Keep looking for a leaf. */
static int
findleaf(void * cookie)
{
	struct findleaf_cookie * C = cookie;
	struct node * NP = NULL;	/* Initialize to keep gcc happy. */
	size_t i;
	int rc;

	/* Sanity-check: We are now at a present node. */
	assert(node_present(C->N));

	/*
	 * Unlock the node we started at.  We don't need this lock as long as
	 * we pick up another lock before we return or make any calls which
	 * might evict a node.
	 */
	btree_node_unlock(C->T, C->N);

	/* Iterate down through parents. */
	while (node_present(C->N) && (C->N->height > C->h)) {
		/* Which child should we iterate into? */
		i = btree_find_child(C->N, C->k);

		/* Iterate into the child. */
		if ((C->e != NULL) && (i < C->N->nkeys)) {
			kvldskey_free(C->e);
			C->e = C->N->u.keys[i];
			kvldskey_ref(C->e);
		}
		NP = C->N;
		C->N = C->N->v.children[i];
	};

	/* If the node is not present, fetch it; else, do the callback. */
	if (!node_present(C->N)) {
		/*
		 * Lock the parent node, to make sure it doesn't get paged
		 * out before it is locked by the child we're paging in.
		 */
		btree_node_lock(C->T, NP);

		/* Fetch the child node.  This adds a lock to the parent. */
		if (btree_node_fetch(C->T, C->N, findleaf, C))
			goto err0;

		/* Release the lock we acquired on the parent node. */
		btree_node_unlock(C->T, NP);

		/* Success! */
		return (0);
	} else {
		/* We must have found a node of height <= h. */
		assert(C->N->height <= C->h);

		/* Lock the node. */
		btree_node_lock(C->T, C->N);

		/* Perform the callback. */
		if (C->e)
			rc = (C->callback_range)(C->cookie, C->N, C->e);
		else
			rc = (C->callback)(C->cookie, C->N);

		/* Free the cookie. */
		mpool_findleaf_free(C);

		/* Return status code from callback. */
		return (rc);
	}

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_find_leaf(T, N, k, callback, cookie):
 * Search for the key ${k} in the subtree of ${T} rooted at the node ${N}.
 * Invoke ${callback}(${cookie}, L) with the node ${L} locked, where ${L} is
 * the node under ${N} where the key ${k} should appear.
 */
int
btree_find_leaf(struct btree * T, struct node * N, const struct kvldskey * k,
    int (* callback)(void *, struct node *), void * cookie)
{
	struct findleaf_cookie * C;

	/* Bake a cookie. */
	if ((C = mpool_findleaf_malloc()) == NULL)
		goto err0;
	C->callback = callback;
	C->callback_range = NULL;
	C->cookie = cookie;
	C->T = T;
	C->N = N;
	C->k = k;
	C->h = 0;
	C->e = NULL;

	/* Lock the node. */
	btree_node_lock(C->T, C->N);

	/* Call into findleaf. */
	if (findleaf(C))
		goto err1;

	/* Success! */
	return (0);

err1:
	mpool_findleaf_free(C);
err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_find_range(T, N, k, h, callback, cookie):
 * Search for a node of height ${h} or less in the subtree of ${T} rooted at
 * ${N} which is responsible for a range including the key ${k}.  Invoke
 * ${callback}(${cookie}, L, e} with the node ${L} locked, where ${L} is the
 * node in question and ${e} is the endpoint of the range for which ${L} is
 * responsible (or "" if ${L} extends to the end of the keyspace).  The
 * callback is responsible for freeing e.
 */
int
btree_find_range(struct btree * T, struct node * N,
    const struct kvldskey * k, int h,
    int (* callback)(void *, struct node *, struct kvldskey *),
    void * cookie)
{
	struct findleaf_cookie * C;

	/* Bake a cookie. */
	if ((C = mpool_findleaf_malloc()) == NULL)
		goto err0;
	C->callback = NULL;
	C->callback_range = callback;
	C->cookie = cookie;
	C->T = T;
	C->N = N;
	C->k = k;
	C->h = h;

	if ((C->e = kvldskey_create(NULL, 0)) == NULL)
		goto err1;

	/* Lock the node. */
	btree_node_lock(C->T, C->N);

	/* Call into findleaf. */
	if (findleaf(C))
		goto err2;

	/* Success! */
	return (0);

err2:
	kvldskey_free(C->e);
err1:
	mpool_findleaf_free(C);
err0:
	/* Failure! */
	return (-1);
}

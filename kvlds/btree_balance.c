#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "imalloc.h"
#include "kvldskey.h"

#include "btree_node.h"
#include "node.h"
#include "serialize.h"

#include "btree.h"

/* Tree-balancing state. */
struct balance_cookie {
	int (* callback)(void *);
	void * cookie;
	struct btree * T;
	size_t nmergefetch;
};

static int merge_fetch(void *);
static int domerge(void *);

/* Split the descendents of the specified node. */
static int
splitchildren(struct btree * T, struct node * N)
{
	size_t i, j, nparts;
	size_t new_nkeys;
	const struct kvldskey ** new_keys;
	struct node ** new_children;
	int failed = 0;		/* We haven't failed yet. */

	/* If this node has no children, do nothing. */
	if (N->type != NODE_TYPE_PARENT)
		goto done;

	/* If this node is not dirty, it doesn't need splitting. */
	if (N->state != NODE_STATE_DIRTY)
		goto done;

	/* Recurse down. */
	for (i = 0; i <= N->nkeys; i++) {
		if (splitchildren(T, N->v.children[i]))
			goto err0;
	}

	/* Figure out how many children we'll have after splitting them. */
	for (new_nkeys = i = 0; i <= N->nkeys; i++) {
		if (node_present(N->v.children[i]) &&
		    (serialize_size(N->v.children[i]) > T->pagelen))
			new_nkeys +=
			    btree_node_split_nparts(T, N->v.children[i]);
		else
			new_nkeys += 1;
	}
	new_nkeys -= 1;		/* Last child doesn't have a separator key. */

	/*
	 * If the number of children won't change, we must not need to split
	 * any nodes; so we have nothing to do.
	 */
	if (new_nkeys == N->nkeys)
		goto done;

	/* This must be more than we had before. */
	assert(new_nkeys > N->nkeys);

	/* Allocate new child and key arrays. */
	if (IMALLOC(new_keys, new_nkeys, const struct kvldskey *))
		goto err0;
	if (IMALLOC(new_children, new_nkeys + 1, struct node *))
		goto err1;

	/*
	 * We're going to walk through the list of children, splitting them
	 * as required, and placing nodes onto the new_children list.  While
	 * doing this, we're also going to place keys onto the new_keys list.
	 * If we fail to split a node, we'll just copy it intact (thus ending
	 * up with an intact tree, albeit one with an overlarge node).
	 */
	for (i = 0, j = 0; i <= N->nkeys; i++, j += nparts) {
		/* If a node is present and overlarge, split it. */
		if (node_present(N->v.children[i]) &&
		    (serialize_size(N->v.children[i]) > T->pagelen)) {
			if (btree_node_split(T, N->v.children[i],
			    &new_keys[j], &new_children[j], &nparts)) {
				/*
				 * Splitting failed.  Just grab the unsplit
				 * node and record that we failed.
				 */
				new_children[j] = N->v.children[i];
				nparts = 1;
				failed = 1;
			}
		} else {
			new_children[j] = N->v.children[i];
			nparts = 1;
		}

		/* Handle the separator, if one exists. */
		if (i < N->nkeys)
			new_keys[j + nparts - 1] = N->u.keys[i];
	}

	/* If we failed, we will have less keys than expected. */
	if (failed)
		new_nkeys = j - 1;

	/* Check that we got the right number of keys. */
	assert(new_nkeys == j - 1);

	/* Free old separator and child vectors. */
	free(N->u.keys);
	free(N->v.children);

	/* Attach new vectors. */
	N->u.keys = new_keys;
	N->v.children = new_children;
	N->nkeys = new_nkeys;

	/* Hook up parent pointers. */
	for (i = 0; i <= N->nkeys; i++) {
		/*
		 * The dirty parent should be either N (non-split node) or
		 * NULL (split parts).
		 */
		assert((N->v.children[i]->p_dirty == NULL) ||
		    (N->v.children[i]->p_dirty == N));

		/* We don't need to do anything if p_dirty is correct. */
		if (N->v.children[i]->p_dirty)
			continue;

		/* If it's NULL, we need to hook it up. */
		N->v.children[i]->p_dirty = N;

		/* ... and add a child lock on N. */
		btree_node_lock(T, N);
	}

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* If we failed to split a node, exit with an error response. */
	if (failed)
		goto err0;

done:
	/* Success! */
	return (0);

err1:
	free(new_keys);
err0:
	/* Failure! */
	return (-1);
}

/* Split a root. */
static struct node *
splitroot(struct btree * T, struct node * N)
{
	size_t nkeys;
	const struct kvldskey ** keys;
	struct node ** children;
	struct node * R;
	size_t nparts;
	size_t i;

	/* Figure out how many separator keys we will have. */
	nkeys = btree_node_split_nparts(T, N) - 1;

	/* Allocate vectors for keys and children. */
	if (IMALLOC(keys, nkeys, const struct kvldskey *))
		goto err0;
	if (IMALLOC(children, nkeys + 1, struct node *))
		goto err1;

	/*
	 * Make sure we're not creating a tree which is insanely tall.  Our
	 * balancing invariants result in the tree width doubling at each
	 * level, so there's no way we can reach height 64 without running
	 * out of pages of storage first.
	 */
	assert(N->height + 1 < 64);

	/* Create a parent node. */
	if ((R = btree_node_mkparent(T, N->height + 1,
	    nkeys, keys, children)) == NULL)
		goto err2;
	T->nnodes++;

	/* Mark the new node as a root. */
	R->root = 1;
	btree_node_lock(T, R);

	/* Remove the root marker on the old root. */
	N->root = 0;
	btree_node_unlock(T, N);

	/* Tell ${N} that it has a new dirty parent. */
	N->p_dirty = R;
	btree_node_lock(T, R);

	/* Split the node, writing keys and new nodes into the new root. */
	if (btree_node_split(T, N, R->u.keys, R->v.children, &nparts))
		goto err3;

	/* Sanity check the number of children. */
	assert(nparts == R->nkeys + 1);

	/* Tell the new children who their parent is. */
	for (i = 0; i <= R->nkeys; i++) {
		/* The child has a dirty parent. */
		R->v.children[i]->p_dirty = R;

		/* The child might have a lock on the parent. */
		if (node_hasplock(R->v.children[i]))
			btree_node_lock(T, R);
	}

	/* Success! */
	return (R);

err3:
	/* Turn ${N} back into a root. */
	N->root = 1;
	btree_node_lock(T, N);

	/* Release the root lock on ${R}. */
	btree_node_unlock(T, R);

	/*
	 * Destroy ${R}, including key and child arrays, but don't free
	 * keys or children (since btree_node_split returns with those
	 * already cleaned up).
	 */
	R->nkeys = (size_t)(-1);
	btree_node_destroy(T, R);
	goto err0;
err2:
	free(children);
err1:
	free(keys);
err0:
	/* Failure! */
	return (NULL);
}

/* Split oversized nodes in the tree. */
static int
splittree(struct btree * T)
{
	struct node * R;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* First, split the children of the root. */
	if (splitchildren(T, T->root_dirty))
		goto err0;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* Next, split the root (if necessary). */
	while (serialize_size(T->root_dirty) > T->pagelen) {
		/* Try to create a new root. */
		if ((R = splitroot(T, T->root_dirty)) == NULL)
			goto err0;

		/* This is now the dirty root. */
		T->root_dirty = R;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Plan merges and fetch pages. */
static int
planmergenode(struct balance_cookie * B, struct node * N)
{
	size_t maxplen = (B->T->pagelen * 2) / 3;
	size_t i;
	size_t plen;
	int gotdirty;
	int leafchild;
	int merging;

	/* If this node has no children, do nothing. */
	if (N->type != NODE_TYPE_PARENT)
		goto done;

	/* If this node is not dirty, it doesn't need splitting. */
	if (N->state != NODE_STATE_DIRTY)
		goto done;

	/* Plan merges under our children first. */
	for (i = 0; i <= N->nkeys; i++) {
		if (planmergenode(B, N->v.children[i]))
			goto err0;
	}

	/* Scan this node to see if we can merge any of its children. */
	/*
	 * Child number N->nkeys can't be merged into a higher-numbered child
	 * so we jump directly to nomerge.  This could be done using an
	 * "are we on the first pass through the loop" test inside the loop,
	 * but this confuses gcc.
	 */
	i = N->nkeys;
	goto nomerge;
	for (/* i = N->nkeys; */; i <= N->nkeys; i--) {
		/* Have we seen a dirty node yet? */
		if (N->v.children[i]->state == NODE_STATE_DIRTY) {
			gotdirty = 1;
			leafchild = (N->v.children[i]->type == NODE_TYPE_LEAF);
		}

		/*
		 * If we haven't seen a dirty node, we can't possibly need to
		 * merge this into the previous node.
		 */
		if (!gotdirty)
			goto nomerge;

		/*
		 * Figure out how large a node we'll produce if we merge this
		 * node into the next one.
		 */
		if (!leafchild)
			plen += kvldskey_serial_size(N->u.keys[i]);
		plen += serialize_merge_size(N->v.children[i]);

		/* Would the resulting node be too big? */
		if (plen > maxplen)
			goto nomerge;

		/* Mark this node as requiring merging. */
		N->v.children[i]->merging = 1;

		/* Move on to considering the next child. */
		continue;

nomerge:
		/*
		 * We're not merging this node into the next one.  Reset the
		 * state we're tracking so we can check if other nodes should
		 * be merged into this one.
		 */
		plen = serialize_size(N->v.children[i]);
		gotdirty = (N->v.children[i]->state == NODE_STATE_DIRTY);
		leafchild = (N->v.children[i]->type == NODE_TYPE_LEAF);
	}

	/* Page in any nodes which are needed for merging. */
	for (merging = i = 0; i <= N->nkeys; i++) {
		/* Do we need this node? */
		if (merging || N->v.children[i]->merging) {
			/* Is it present? */
			if (node_present(N->v.children[i])) {
				/* Make sure it stays present. */
				btree_node_lock(B->T, N->v.children[i]);
			} else {
				/* Fetch it. */
				B->nmergefetch += 1;
				if (btree_node_fetch(B->T, N->v.children[i],
				    merge_fetch, B))
					goto err0;
			}
		}

		/* Are we in the middle of a merging range? */
		merging = N->v.children[i]->merging;
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* A node has been fetched for merging. */
static int
merge_fetch(void * cookie)
{
	struct balance_cookie * B = cookie;

	/* One fetch done, N-1 left to go. */
	B->nmergefetch -= 1;

	/* Call domerge if we've got everything. */
	if (B->nmergefetch == 0) {
		if (!events_immediate_register(domerge, B, 1))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Plan merging of undersized nodes in the tree. */
static int
planmerge(struct balance_cookie * B)
{
	struct btree * T = B->T;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* We're not fetching any nodes yet. */
	B->nmergefetch = 0;

	/*
	 * Figure out what merging (if any) needs to be done, and fetch pages
	 * (if any) required in order to do it.
	 */
	if (planmergenode(B, T->root_dirty))
		goto err0;

	/* If we don't need to fetch any nodes, call into domerge next. */
	if (B->nmergefetch == 0) {
		if (!events_immediate_register(domerge, B, 1))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Perform planned merges in a subtree. */
static int
domergenode(struct balance_cookie * B, struct node * N)
{
	size_t nmerges, i, j;
	size_t nmerge;
	int failed = 0;		/* We haven't failed yet. */
	int merging;
	struct node * NC;

	/*
	 * As far as we know, we've done all possibly merges under this
	 * subtree.  (We'll adjust this later if necessary).
	 */
	N->needmerge = 0;

	/* If this node is not dirty, it doesn't need merging. */
	if (N->state != NODE_STATE_DIRTY)
		goto done;

	/* If this node has no children, do nothing. */
	if (N->type != NODE_TYPE_PARENT)
		goto done;

	/* Perform merges under our children first. */
	for (i = 0; i <= N->nkeys; i++) {
		/* Perform merges under this child. */
		if (domergenode(B, N->v.children[i]))
			goto err0;

		/* If this child needs to be remerged, so do we. */
		if (N->v.children[i]->needmerge)
			N->needmerge = 1;
	}

	/* Count how many of our children will be merged. */
	for (nmerges = i = 0; i <= N->nkeys; i++) {
		if (N->v.children[i]->merging) {
			/* We have a merge required. */
			nmerges++;

			/* We might need to do a merge in the new child. */
			N->needmerge = 1;
		}
	}

	/* If we don't need to do any merges, we're done. */
	if (nmerges == 0)
		goto done;

	/* Iterate through the children, dirtying and unlocking them. */
	for (merging = 0, i = 0;
	    i <= N->nkeys;
	    merging = N->v.children[i]->merging, i++) {
		/*
		 * Grab a pointer to the node.  Note that if we dirty NC, it
		 * will no longer be the same as N->v.children[i].
		 */
		NC = N->v.children[i];

		/* If this node isn't being merged, move on. */
		if ((NC->merging == 0) && (merging == 0))
			continue;

		/* If this node isn't dirty, make it dirty. */
		if (NC->state != NODE_STATE_DIRTY) {
			if (btree_node_dirty(B->T, NC) == NULL)
				failed = 1;

			/* The dirty node is being merged, not the shadow. */
			N->v.children[i]->merging = NC->merging;
			NC->merging = 0;
		}

		/*
		 * Release the lock we picked up in planmergenode or which
		 * was picked up for us by btree_node_fetch.
		 */
		btree_node_unlock(B->T, NC);
	}

	/* If we failed to dirty a node, give up now. */
	if (failed)
		goto err0;

	/* Iterate through the children, merging them as necessary. */
	for (nmerge = 0, j = i = 0; i <= N->nkeys; i++) {
		/* If this node is being merged into the next, move on. */
		if (N->v.children[i]->merging) {
			nmerge++;
			continue;
		}

		/* If we're not merging, just copy the child and key. */
		if (nmerge == 0) {
			N->v.children[j] = N->v.children[i];
			if (i < N->nkeys)
				N->u.keys[j] = N->u.keys[i];
			j += 1;
			continue;
		}

		/* Merge children. */
		if (btree_node_merge(B->T,
		    &N->v.children[i - nmerge], &N->u.keys[i - nmerge],
		    &N->v.children[j], &N->u.keys[j], nmerge)) {
			j += nmerge;
			failed = 1;
		}

		/* Copy the separator key. */
		if (i < N->nkeys)
			N->u.keys[j] = N->u.keys[i];
		j += 1;

		/* We've merged this batch; start over. */
		nmerge = 0;
	}

	/* Store number of keys. */
	N->nkeys = j - 1;

	/* The size of this node has changed. */
	N->pagesize = (uint32_t)(-1);

	/* Did we fail? */
	if (failed)
		goto err0;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Remove extraneous root nodes. */
static void
deroot(struct balance_cookie * B)
{
	struct btree * T = B->T;
	struct node * R;

	/* Repeat until the root is a leaf or has 2+ children. */
	while ((T->root_dirty->type == NODE_TYPE_PARENT) &&
	    (T->root_dirty->nkeys == 0)) {
		/* Grab a pointer to the root node. */
		R = T->root_dirty;

		/* Promote its child to roothood. */
		T->root_dirty = R->v.children[0];
		T->root_dirty->root = 1;
		T->root_dirty->pagesize = (uint32_t)(-1);
		btree_node_lock(T, T->root_dirty);
		T->root_dirty->p_dirty = NULL;

		/* The old root is no longer a root. */
		R->root = 0;
		btree_node_unlock(T, R);

		/* The old root no longer has a child. */
		R->v.children[0] = NULL;
		btree_node_unlock(T, R);

		/* Free the old root node. */
		btree_node_destroy(T, R);
		T->nnodes--;
	}
}

/* Perform the merges planned by planmerge. */
static int
domerge(void * cookie)
{
	struct balance_cookie * B = cookie;
	struct btree * T = B->T;

	/* We should have all the nodes we need. */
	assert(B->nmergefetch == 0);

	/* Do the merging. */
	if (domergenode(B, T->root_dirty))
		goto err0;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* If the tree is not fully merged, plan more merges. */
	if (T->root_dirty->needmerge) {
		if (planmerge(B))
			goto err0;
	} else {
		/* Remove extraneous root nodes. */
		deroot(B);

		/* We're done!  Schedule the callback and free the cookie. */
		if (!events_immediate_register(B->callback, B->cookie, 0))
			goto err0;
		free(B);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_balance(T, callback, cookie):
 * Rebalance the B+Tree ${T}, and invoke the provided callback.
 */
int
btree_balance(struct btree * T, int (* callback)(void *), void * cookie)
{
	struct balance_cookie * B;

	/* Bake a cookie. */
	if ((B = malloc(sizeof(struct balance_cookie))) == NULL)
		goto err0;
	B->callback = callback;
	B->cookie = cookie;
	B->T = T;

	/* Split nodes as necessary. */
	if (splittree(T))
		goto err1;

	/* Merge nodes as necessary and perform callback. */
	if (planmerge(B))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(B);
err0:
	/* Failure! */
	return (-1);
}

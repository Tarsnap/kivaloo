#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "imalloc.h"
#include "proto_lbs.h"
#include "warnp.h"

#include "btree_node.h"
#include "node.h"
#include "serialize.h"

#include "btree.h"

struct write_cookie {
	/* Callback to be performed after sync is done. */
	int (*callback)(void *);
	void * cookie;

	/* The B+Tree. */
	struct btree * T;
};

static int callback_append(void *, int, int, uint64_t);
static int callback_unshadow(void *);

/* Count the number of dirty nodes under the specified node. */
static size_t
ndirty(struct node * N)
{
	size_t n, i;

	/* If this node is not dirty, there are no dirty nodes. */
	if (N->state != NODE_STATE_DIRTY)
		return (0);

	/* If this node is not a parent, it is the only dirty node. */
	if (N->type != NODE_TYPE_PARENT)
		return (1);

	/* Otherwise, we have 1 + the sum of children. */
	for (n = 1, i = 0; i <= N->nkeys; i++)
		n += ndirty(N->v.children[i]);
	return (n);
}

/* Serialize the dirty nodes in a (sub)tree. */
static int
serializetree(struct btree * T, struct node * N, size_t pagelen,
    uint64_t nextblk, const uint8_t ** bufv, uint64_t * pn)
{
	size_t i;

	/* If this node is not dirty, return immediately. */
	if (N->state != NODE_STATE_DIRTY)
		return (0);

	/* If this node has children, serialize them first. */
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++)
			if (serializetree(T, N->v.children[i], pagelen,
			    nextblk, bufv, pn))
				goto err0;
	}

	/* Record this node's page number. */
	N->pagenum = nextblk + *pn;

	/*
	 * Figure out what the oldest leaf number under this node is.  The
	 * oldest not-being-cleaned leaf is computed in makeclean.
	 */
	N->oldestleaf = N->pagenum;
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++) {
			if (N->v.children[i]->oldestleaf < N->oldestleaf)
				N->oldestleaf = N->v.children[i]->oldestleaf;
		}
	}

	/* Serialize the page and record the page pointer. */
	if (serialize(T, N, pagelen))
		goto err0;
	bufv[(*pn)++] = N->pagebuf;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Mark all dirty nodes in a (sub)tree as clean. */
static void
makeclean(struct btree * T, struct node * N)
{
	size_t i;

	/* Sanity-check: We should not have reached a shadow node. */
	assert(N->state != NODE_STATE_SHADOW);

	/* If this node is not dirty, return immediately. */
	if (N->state != NODE_STATE_DIRTY)
		return;

	/* If this node has children, recurse down. */
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++)
			makeclean(T, N->v.children[i]);
	}

	/*
	 * Figure out what the page number of the oldest leaf under this node
	 * which isn't currently being cleaned is.  (We computed the overall
	 * oldest leaf during serializetree since it gets written out.)
	 */
	N->oldestncleaf = N->pagenum;
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++) {
			if (N->v.children[i]->oldestncleaf < N->oldestncleaf)
				N->oldestncleaf =
				    N->v.children[i]->oldestncleaf;
		}
	}

	/* Mark this node as clean. */
	N->state = NODE_STATE_CLEAN;

	/* Remove the node-is-dirty lock on the node. */
	btree_node_unlock(T, N);

	/* This node's dirty parent is also its shadow parent. */
	N->p_shadow = N->p_dirty;
	btree_node_lock(T, N->p_shadow);
}

/* Free shadow nodes and reparent clean children. */
static void
unshadow(struct btree * T, struct node * N)
{
	size_t i;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* Sanity-check: We should not have reached a dirty node. */
	assert(N->state != NODE_STATE_DIRTY);

	/* If this node is clean, reparent it and return. */
	if (N->state == NODE_STATE_CLEAN) {
		/* Do we need to release a lock on our shadow parent? */
		if (node_hasplock(N))
			btree_node_unlock(T, N->p_shadow);

		/* Our dirty parent is our only parent. */
		N->p_shadow = N->p_dirty;

		/* Acquire a lock on our new shadow parent. */
		if (node_hasplock(N))
			btree_node_lock(T, N->p_shadow);

		/* We're done. */
		return;
	}

	/* If this node has children, recurse down. */
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++) {
			/* Recurse down. */
			unshadow(T, N->v.children[i]);

			/* Clear the child pointer. */
			N->v.children[i] = NULL;
		}
	}

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* Destroy this node. */
	btree_node_destroy(T, N);
}

/**
 * btree_sync(T, callback, cookie):
 * Serialize and write dirty nodes from the B+Tree ${T}; mark said nodes as
 * clean; free the shadow tree; and invoke the provided callback.
 */
int
btree_sync(struct btree * T, int (* callback)(void *), void * cookie)
{
	struct write_cookie * WC;
	size_t npages;
	const uint8_t ** bufv;
	uint64_t pn = 0;

	/* Bake a cookie. */
	if ((WC = malloc(sizeof(struct write_cookie))) == NULL)
		goto err0;
	WC->T = T;
	WC->callback = callback;
	WC->cookie = cookie;

	/* Figure out how many pages we need to write. */
	npages = ndirty(T->root_dirty);

	/* Allocate a vector to hold pointers to pages. */
	if (IMALLOC(bufv, npages, const uint8_t *))
		goto err1;

	/* Serialize pages and record pointers into the vector. */
	if (serializetree(T, T->root_dirty, T->pagelen, T->nextblk,
	    bufv, &pn))
		goto err2;

	/* Sanity check the number of pages serialized. */
	assert(pn == npages);

	/* Write pages out. */
	if (proto_lbs_request_append_blks(T->LBS, npages, T->nextblk,
	    T->pagelen, bufv, callback_append, WC)) {
		warnp("Error writing pages");
		goto err2;
	}

	/* Free the page pointers vector. */
	free(bufv);

	/* Success! */
	return (0);

err2:
	free(bufv);
err1:
	free(WC);
err0:
	/* Failure! */
	return (-1);
}

/* Callback for btree_sync when write is complete. */
static int
callback_append(void * cookie, int failed, int status, uint64_t blkno)
{
	struct write_cookie * WC = cookie;
	struct btree * T = WC->T;

	/* Throw a fit if we didn't manage to write the pages. */
	if (failed)
		goto err1;
	if (status) {
		warn0("Failed to write dirty nodes to backing store");
		goto err1;
	}

	/* Record the next available block number. */
	T->nextblk = blkno;

	/* Mark the nodes in the dirty tree as clean. */
	makeclean(T, T->root_dirty);

	/*
	 * Make sure no callbacks are pending on the shadow tree before we
	 * garbage collect it.
	 */
	if (!events_immediate_register(callback_unshadow, WC, 1))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(WC);

	/* Failure! */
	return (-1);
}

/* Kill old shadow tree. */
static int
callback_unshadow(void * cookie)
{
	struct write_cookie * WC = cookie;
	struct btree * T = WC->T;
	struct node * root_shadow;

	/*
	 * Grab the root of the shadow tree, and use the (now clean) dirty
	 * tree as the shadow tree henceforth.
	 */
	root_shadow = T->root_shadow;
	T->root_shadow = T->root_dirty;
	btree_node_lock(T, T->root_shadow);

	/* Kill the old shadow tree, if there was one. */
	if (root_shadow != NULL) {
		/* This isn't a root any more, so release the root lock. */
		btree_node_unlock(T, root_shadow);

		/*
		 * Traverse the tree, re-pointing clean children at their
		 * dirty parents and freeing shadow nodes.
		 */
		unshadow(T, root_shadow);
	}

	/* Update number-of-pages-used value. */
	T->npages = T->nextblk - T->root_dirty->oldestleaf;

	/*
	 * We could issue a FREE call here, but since FREE is only advisory
	 * we need to call it elsewhere as well in order to avoid having data
	 * permanently stored even when it could all be freed.  Since we're
	 * calling FREE elsewhere anyway, don't bother calling it here.
	 */

	/* Register post-sync callback to be performed. */
	if (!events_immediate_register(WC->callback, WC->cookie, 0))
		goto err1;

	/* Free cookie. */
	free(WC);

	/* Success! */
	return (0);

err1:
	free(WC);

	/* Failure! */
	return (-1);
}

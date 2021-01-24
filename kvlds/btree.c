#include <sys/time.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "pool.h"
#include "proto_lbs.h"
#include "warnp.h"
#include "wire.h"

#include "btree_cleaning.h"
#include "btree_node.h"
#include "node.h"
#include "serialize.h"

#include "btree.h"

struct params_cookie {
	struct btree * T;
	uint64_t lastblk;
	int failed;
	int done;
};

struct getroot_cookie {
	int done;
};

struct sync_cookie {
	int done;
};

/* Time between FREE calls. */
static const struct timeval free_time = {
	.tv_sec = 1,
	.tv_usec = 0
};

/* Callback for PARAMS2 request. */
static int
callback_params(void * cookie, int failed, size_t blklen, uint64_t blkno,
    uint64_t lastblk)
{
	struct params_cookie * C = cookie;

	/* Record returned values. */
	C->T->pagelen = blklen;
	C->T->nextblk = blkno;
	C->lastblk = lastblk;

	/* We're done. */
	C->failed = failed;
	C->done = 1;

	/* Success! */
	return (0);
}

/* Callback for root page GET request. */
static int
callback_getroot(void * cookie)
{
	struct getroot_cookie * GC = cookie;

	/* We're done. */
	GC->done = 1;

	/* Success! */
	return (0);
}

/* Callback for root page sync. */
static int
callback_sync(void * cookie)
{
	struct sync_cookie * SC = cookie;

	/* We're done. */
	SC->done = 1;

	/* Success! */
	return (0);
}

/* Callback for FREE request sent to the backing store. */
static int
callback_free_done(void * cookie, int failed)
{

	(void)cookie; /* UNUSED */

	/* Throw a fit if the FREE failed. */
	if (failed) {
		warn0("FREE request failed");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback for periodic FREE calls. */
static int
callback_gc(void * cookie)
{
	struct btree * T = cookie;

	/* The timer is no longer scheduled. */
	T->gc_timer = NULL;

	/*
	 * Instruct the backing store to free everything older than the
	 * oldest leaf node accessible via the B+Tree root.
	 */
	if (proto_lbs_request_free(T->LBS, T->root_shadow->oldestleaf,
	    callback_free_done, NULL))
		goto err0;

	/* Schedule another FREE. */
	if ((T->gc_timer =
	    events_timer_register(callback_gc, T, &free_time)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_init(Q_lbs, npages, npagebytes, keylen, vallen, Scost):
 * Initialize a B+Tree with backing store accessible by sending requests via
 * the request queue ${Q_lbs}.  Aim to keep (in order of preference) at most
 * ${npages}, ${npagebytes} / pagelen, or 1024 nodes of the tree in RAM at a
 * time.  Verify that keys of length ${keylen} and values of length ${vallen}
 * can be used with the available page size; or set the variables to sensible
 * default values.  Storing a GB of data for a month costs roughly ${Scost}
 * times as much as performing 10^6 I/Os.
 *
 * This function may call events_run internally.
 */
struct btree *
btree_init(struct wire_requestqueue * Q_lbs, uint64_t npages,
    uint64_t npagebytes, uint64_t * keylen, uint64_t * vallen, double Scost)
{
	struct btree * T;
	struct node * C;
	struct params_cookie PC;
	struct getroot_cookie GC;
	struct sync_cookie SC;
	uint64_t rootblk;
	size_t i;

	/* Sanity check: at least one of {npages, npagebytes} must be unset. */
	assert((npages == (uint64_t)(-1)) || (npagebytes == (uint64_t)(-1)));

	/* Sanity check: must be able to fit npagebytes in memory. */
	assert((npagebytes == (uint64_t)(-1)) || (npagebytes <= SIZE_MAX));

	/* Sanity check: must be able to fit npages in memory. */
	assert((npages == (uint64_t)(-1)) || (npages <= SIZE_MAX));

	/* Allocate space for the B+Tree. */
	if ((T = malloc(sizeof(struct btree))) == NULL)
		goto err0;

	/* Attach LBS request queue to the tree. */
	T->LBS = Q_lbs;

	/* Issue a PARAMS2 request. */
	PC.T = T;
	PC.failed = PC.done = 0;
	if (proto_lbs_request_params2(T->LBS, callback_params, &PC)) {
		warnp("Failed to send PARAMS2 request");
		goto err1;
	}
	if (events_spin(&PC.done) || PC.failed) {
		warnp("PARAMS2 request failed");
		goto err1;
	}

	/* If we have neither npages nor npagebytes, set a default. */
	if ((npages == (uint64_t)(-1)) && (npagebytes == (uint64_t)(-1))) {
#if 128 * 1024 * 1024 < SIZE_MAX
		/* Normal default is 128 MB. */
		npagebytes = 128 * 1024 * 1024;
#else
		/* Set a smaller default size for systems with a tiny size_t. */
		npagebytes = SIZE_MAX;
#endif
	}

	/* Figure out how many pages to use and sanity-check. */
	if (npagebytes != (uint64_t)(-1)) {
		npages = npagebytes / T->pagelen;
		if ((npages < 1024) || (npages > 1024 * 1024 * 1024)) {
			warn0("Cache size in pages must be in [2^10, 2^30]");
			goto err1;
		}
	}
	T->poolsz = (size_t)npages;

	/* Set default key/value lengths if necessary. */
	if (*keylen == (uint64_t)(-1)) {
		if (T->pagelen < 1024)
			*keylen = 64;
		else if (T->pagelen < 2048)
			*keylen = 128;
		else
			*keylen = 255;
	}
	if (*vallen == (uint64_t)(-1)) {
		if (T->pagelen < 1024)
			*vallen = 96;
		else if (T->pagelen < 2048)
			*vallen = 192;
		else
			*vallen = 255;
	}

	/* Sanity-check key and value lengths. */
	if (*keylen + *vallen + 2 > T->pagelen / 3) {
		warn0("Key or value lengths too large for page size");
		goto err1;
	}
	if (*keylen * 3 + 3 + SERIALIZE_PERCHILD * 4 + SERIALIZE_OVERHEAD >
	    T->pagelen * 2 / 3) {
		warn0("Key length too large for page size");
		goto err1;
	}

	/* Create a page pool. */
	if ((T->P = pool_init(T->poolsz,
	    offsetof(struct node, pool_cookie))) == NULL)
		goto err1;

	/* No root nodes yet. */
	T->root_shadow = T->root_dirty = NULL;

	/*
	 * Try to find a root node by scanning backwards from the last block
	 * the block store reports having present.
	 */
	for (rootblk = PC.lastblk; rootblk < T->nextblk; rootblk--) {
		/*
		 * Create a node.  If we keep this node, we will fill in the
		 * oldestleaf and pagesize values later.
		 */
		if ((T->root_dirty = node_alloc(rootblk, (uint64_t)(-1),
		    (uint32_t)(-1))) == NULL) {
			warnp("Failed to allocate node");
			goto err2;
		}
		T->root_shadow = T->root_dirty;

		/* Page in the node data. */
		GC.done = 0;
		if (btree_node_fetch_try(T, T->root_dirty,
		    callback_getroot, &GC)) {
			warnp("Failed to GET root page");
			goto err3;
		}

		/* Wait until we've finished fetching. */
		if (events_spin(&GC.done)) {
			warnp("Error reading root page");

			/*
			 * If events_spin failed, we don't know what state
			 * the root node is in, so we can't safely free it.
			 */
			goto err1;
		}

		/* Is this a root node? */
		if (node_present(T->root_dirty) && T->root_dirty->root)
			break;

		/* Not a root node; free this and try the next one. */
		btree_node_destroy(T, T->root_dirty);
		T->root_dirty = NULL;
	}

	/* If we have found a root node, finish up initialization. */
	if (T->root_dirty != NULL) {
		/* Record the size of the serialized node. */
		T->root_dirty->pagesize =
		    (uint32_t)serialize_size(T->root_dirty);

		/* Figure out the oldestleaf. */
		if (T->root_dirty->type == NODE_TYPE_PARENT) {
			for (i = 0; i <= T->root_dirty->nkeys; i++) {
				C = T->root_dirty->v.children[i];
				if (C->oldestleaf <
				    T->root_dirty->oldestleaf)
					T->root_dirty->oldestleaf =
					    C->oldestleaf;
			}
		} else {
			T->root_dirty->oldestleaf = T->root_dirty->pagenum;
		}

		/* The oldest not-being-cleaned leaf is the oldest leaf. */
		T->root_dirty->oldestncleaf = T->root_dirty->oldestleaf;

		/* Figure out how many pages we're using. */
		T->npages = T->nextblk - T->root_dirty->oldestleaf;

		/* This is also our shadow root. */
		T->root_shadow = T->root_dirty;
		btree_node_lock(T, T->root_shadow);

		/* We have a root! */
		goto gotroot;
	}

	/* If we had any pages, one of them should have been a root. */
	if (T->nextblk > 0) {
		warn0("Could not find root B+Tree node");
		goto err2;
	}

	/* Create a dirty leaf node. */
	if ((T->root_dirty = btree_node_mkleaf(T, 0, NULL)) == NULL)
		goto err2;

	/* Mark the node as a root. */
	T->root_dirty->root = 1;
	btree_node_lock(T, T->root_dirty);

	/* This tree contains 1 node. */
	T->nnodes = 1;

	/* Sync the (trivial) dirty tree out. */
	SC.done = 0;
	if (btree_sync(T, callback_sync, &SC)) {
		warnp("Failed to APPEND root page");
		goto err4;
	}

	/* Wait until we've finished writing. */
	if (events_spin(&SC.done)) {
		warnp("Error writing root page");

		/*
		 * If events_spin failed, we don't know what state
		 * the root nodes are in, so we can't free them.
		 */
		goto err1;
	}

	/*
	 * If the next writable block is not block #1, we're using a sparse
	 * block space and need to disable cleaning (see comment above).
	 */
	if (T->nextblk != 1)
		Scost = 0.0;

gotroot:
	/* Schedule a callback to invoke FREE. */
	if ((T->gc_timer =
	    events_timer_register(callback_gc, T, &free_time)) == NULL) {
		btree_free(T);
		goto err0;
	}

	/* Start background cleaning. */
	if ((T->cstate = btree_cleaning_start(T, Scost)) == NULL) {
		warnp("Cannot start background cleaning");
		exit(1);
	}

	/* Success! */
	return (T);

	/* Root-creation path. */
err4:
	btree_node_unlock(T, T->root_dirty);
	btree_node_destroy(T, T->root_dirty);
	goto err2;

	/* Root-fetching path. */
err3:
	node_free(T->root_dirty);

	/* Merged exit path. */
err2:
	pool_free(T->P);
err1:
	free(T);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * btree_free(T):
 * Free the B-Tree ${T}, which must have root_shadow == root_dirty and must
 * have no pages locked other than the root node.
 */
void
btree_free(struct btree * T)
{

	/* Sanity-check. */
	assert(T->root_shadow == T->root_dirty);

	/* Shut down the background cleaner. */
	btree_cleaning_stop(T->cstate);

	/* Kill the garbage collection timer. */
	if (T->gc_timer != NULL)
		events_timer_cancel(T->gc_timer);

	/* Release the root locks. */
	btree_node_unlock(T, T->root_shadow);
	btree_node_unlock(T, T->root_dirty);

	/* Page out all pages in the tree. */
	btree_node_pageout_recursive(T, T->root_shadow);

	/* Free the (paged-out) root node. */
	node_free(T->root_shadow);

	/* Free the page pool. */
	pool_free(T->P);

	/* Free the tree structure. */
	free(T);
}

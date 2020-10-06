#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"
#include "events.h"
#include "imalloc.h"
#include "kvldskey.h"
#include "kvpair.h"
#include "pool.h"
#include "proto_lbs.h"
#include "warnp.h"

#include "btree.h"
#include "btree_cleaning.h"
#include "node.h"
#include "serialize.h"

#include "btree_node.h"

/* Reader callback. */
struct reader {
	int (*callback)(void *);
	void * cookie;
};

ELASTICARRAY_DECL(READERLIST, readerlist, struct reader);

/* Read-in-progress state. */
struct reading {
	READERLIST list;	/* List of struct reader callbacks. */
	struct btree * T;	/* B+tree to which this page belongs. */
	size_t pagelen;		/* Size of page. */
	int canfail;		/* Non-zero if failure is an option. */
};

/* Descend-into-node state. */
struct descend {
	int (*callback)(void *, struct node *);
	void * cookie;
	struct node * N;
};

static int callback_fetch(void *, int, int, const uint8_t *);
static int callback_descend(void *);

/**
 * freedata(T, N):
 * Free a node's data and mark the node as not present.  The node must not be
 * in the node pool and must be present.
 */
static void
freedata(struct btree * T, struct node * N)
{
	size_t i;

	/* Sanity check. */
	assert(node_present(N));

	/* If the node has data, free it. */
	if (N->nkeys != (size_t)(-1)) {
		/* Leaf or parent? */
		if (N->type == NODE_TYPE_LEAF) {
			/* Free array of key-value pairs. */
			free(N->u.pairs);
		} else {
			/* Free array of keys. */
			free(N->u.keys);

			/* Free children and their array. */
			for (i = 0; i <= N->nkeys; i++)
				node_free(N->v.children[i]);
			free(N->v.children);
		}

		/* This node no longer has any data. */
		N->nkeys = (size_t)(-1);
	}

	/* If the node has a serialized buffer, free it. */
	if (N->pagebuf) {
		free(N->pagebuf);
		N->pagebuf = NULL;
	}

	/* We just removed a reason for keeping the parent(s) present. */
	btree_node_unlock(T, N->p_shadow);
	btree_node_unlock(T, N->p_dirty);

	/* This node now has indeterminate height. */
	N->height = -1;

	/* Mark the node as not present. */
	N->type = NODE_TYPE_NP;
}

/* Add a node to the page pool and handle any resulting eviction. */
static int
makepresent(struct btree * T, struct node * N)
{
	void * evict;
	struct node * N_evict;

	/* Add the node to the pool. */
	if (pool_rec_add(T->P, N, &evict))
		goto err0;
	N_evict = evict;

	/* If a node was evicted, make it non-present. */
	if (N_evict != NULL) {
		/* Sanity-check: We can only evict clean nodes. */
		assert(N_evict->state == NODE_STATE_CLEAN);

		/* Delete the node's data and mark it as non-present. */
		freedata(T, N_evict);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_node_mknode(T, type, height, nkeys, keys, children, pairs):
 * Create and return a new dirty node with lock count 1 belonging to the
 * B+Tree ${T}, of type ${type}, height ${height}, ${nkeys} keys; if a parent
 * then the keys are in ${keys} and children are in ${children}; if a leaf
 * then the key-value pairs are in ${pairs}.
 */
struct node *
btree_node_mknode(struct btree * T, unsigned int type, int height, size_t nkeys,
    const struct kvldskey ** keys, struct node ** children,
    struct kvpair_const * pairs)
{
	struct node * N;

	/* Sanity check. */
	assert((height <= INT8_MAX) && (height >= -1));

	/* Allocate node. */
	if ((N = node_alloc((uint64_t)(-1), (uint64_t)(-1), (uint32_t)(-1)))
	    == NULL)
		goto err0;

	/* Make the node present. */
	if (makepresent(T, N))
		goto err1;

	/* This is a DIRTY node of the indicated type and height. */
	N->state = NODE_STATE_DIRTY;
	N->type = type;
	N->height = (int8_t)height;

	/* Store node data. */
	N->nkeys = nkeys;
	if (type == NODE_TYPE_LEAF) {
		N->u.pairs = pairs;
		if ((N->nkeys > 0) && (pairs != NULL)) {
			N->mlen_n = (uint8_t)kvldskey_mlen(N->u.pairs[0].k,
			    N->u.pairs[N->nkeys - 1].k);
		} else {
			N->mlen_n = 255;
		}
	} else {
		N->u.keys = keys;
		N->v.children = children;
	}

	/* We don't know how far keys in this subtree match. */
	N->mlen_t = 0;

	/* Success! */
	return (N);

err1:
	node_free(N);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * btree_node_fetch_canfail(T, N, callback, cookie, canfail):
 * Fetch the node ${N} which is currently of type either NODE_TYPE_NP or
 * NODE_TYPE_READ in the B+Tree ${T}.  Invoke ${callback}(${cookie}) when
 * complete, with the node locked.  If ${canfail} is non-zero, treat a
 * "this page does not exist" response as a non-error.
 */
static int
btree_node_fetch_canfail(struct btree * T, struct node * N,
    int (* callback)(void *), void * cookie, int canfail)
{
	struct reader r;

	/* Sanity check. */
	assert((N->type == NODE_TYPE_NP) || (N->type == NODE_TYPE_READ));

	/* If we're not already reading, do so. */
	if (N->type == NODE_TYPE_NP) {
		/* Make this page present. */
		if (makepresent(T, N))
			goto err0;
		btree_node_lock(T, N->p_shadow);
		btree_node_lock(T, N->p_dirty);

		/* Create a read-in-progress structure. */
		if ((N->u.reading = malloc(sizeof(struct reading))) == NULL)
			goto err1;
		N->u.reading->pagelen = T->pagelen;
		N->u.reading->T = T;
		N->u.reading->canfail = canfail;

		/* Create a list of reader callbacks. */
		if ((N->u.reading->list = readerlist_init(0)) == NULL)
			goto err2;

		/* Read the page. */
		if (proto_lbs_request_get(T->LBS, N->pagenum, T->pagelen,
		    callback_fetch, N))
			goto err3;

		/* This page is now being read. */
		N->type = NODE_TYPE_READ;
	}

	/* If we can't fail, mark the read as such. */
	if (!canfail)
		N->u.reading->canfail = 0;

	/* Add our callback to the list. */
	r.callback = callback;
	r.cookie = cookie;
	if (readerlist_append(N->u.reading->list, &r, 1))
		goto err0;

	/* Add a lock for this callback. */
	btree_node_lock(T, N);

	/* Success! */
	return (0);

err3:
	readerlist_free(N->u.reading->list);
err2:
	free(N->u.reading);
err1:
	pool_rec_free(T->P, N);
	N->pool_cookie = NULL;
err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_node_fetch(T, N, callback, cookie):
 * Fetch the node ${N} which is currently of type either NODE_TYPE_NP or
 * NODE_TYPE_READ in the B+Tree ${T}.  Invoke ${callback}(${cookie}) when
 * complete, with the node locked.
 */
int
btree_node_fetch(struct btree * T, struct node * N,
    int (* callback)(void *), void * cookie)
{

	return (btree_node_fetch_canfail(T, N, callback, cookie, 0));
}

/**
 * btree_node_fetch_try(T, N, callback, cookie):
 * As btree_node_fetch(), but if the page does not exist the callback will be
 * performed with the node not present.
 */
int
btree_node_fetch_try(struct btree * T, struct node * N,
    int (* callback)(void *), void * cookie)
{

	return (btree_node_fetch_canfail(T, N, callback, cookie, 1));
}

#ifdef SANITY_CHECKS
/**
 * btree_node_fetch_lockcount(N):
 * Return the number of locks held on ${N} by fetch callbacks.
 */
size_t
btree_node_fetch_lockcount(struct node * N)
{

	/* Only applicable for readers. */
	assert(N->type == NODE_TYPE_READ);

	return (readerlist_getsize(N->u.reading->list) + 1);
}
#endif

/* Parse the read page and invoke callbacks. */
static int
callback_fetch(void * cookie, int failed, int status, const uint8_t * buf)
{
	struct node * N = cookie;
	struct reading * R = N->u.reading;
	struct reader * r;
	size_t i;

	/* Throw a fit if the read request failed. */
	if (failed) {
		warnp("LBS GET request failed");
		goto err2;
	}

	/* Throw a fit if the block does not exist and we can't fail. */
	if ((status != 0) && (R->canfail == 0)) {
		warn0("Failed to read a mandatory page");
		goto err2;
	}

	/* If the block exists, parse it. */
	if (status == 0) {
		/* Parse the page. */
		if (deserialize(N, buf, R->pagelen)) {
			warn0("Cannot deserialize page");
			goto err2;
		}

		/* If this was a root, parse global tree data. */
		if (N->root) {
			if (deserialize_root(R->T, buf)) {
				warn0("Error parsing root data");
				goto err2;
			}
		}

		/* Release our lock on the page. */
		btree_node_unlock(R->T, N);
	} else {
		/* Otherwise, mark it back as non-present. */
		N->type = NODE_TYPE_NP;

		/* Unlock the node's parents. */
		btree_node_unlock(R->T, N->p_shadow);
		btree_node_unlock(R->T, N->p_dirty);

		/* Release our lock on the page. */
		btree_node_unlock(R->T, N);

		/* Remove from the node pool. */
		pool_rec_free(R->T->P, N);
		N->pool_cookie = NULL;
	}

	/* Schedule callbacks. */
	for (i = 0; i < readerlist_getsize(R->list); i++) {
		/* Get a reader structure. */
		r = readerlist_get(R->list, i);

		/* Schedule a callback. */
		if (!events_immediate_register(r->callback, r->cookie, 0))
			goto err1;
	}

	/* We've finished reading this page. */
	readerlist_free(R->list);
	free(R);

	/* Success! */
	return (0);

err2:
	btree_node_unlock(R->T, N);
err1:
	readerlist_free(R->list);
	free(R);

	/* Failure! */
	return (-1);
}

/**
 * btree_node_destroy(T, N):
 * Remove the node ${N} from the B+Tree ${T} and free it.  If present, the
 * node must have lock count 1 and must not be in the process of being
 * fetched.  Note that this function does not remove dangling pointers from
 * any parent(s).
 */
void
btree_node_destroy(struct btree * T, struct node * N)
{

	/* Sanity-check: We can't free a node we're fetching. */
	assert(N->type != NODE_TYPE_READ);

	/* Make the page non-present. */
	if (N->type != NODE_TYPE_NP) {
		/* Remove from the node pool. */
		pool_rec_free(T->P, N);
		N->pool_cookie = NULL;

		/* Free the node data. */
		freedata(T, N);
	}

	/* Free node. */
	node_free(N);
}

/**
 * btree_node_pageout_recursive(T, N):
 * Recursively page out the node ${N} and its children from the B+Tree ${T}.
 * The nodes must not have any locks aside from child locks.
 */
void
btree_node_pageout_recursive(struct btree * T, struct node * N)
{
	size_t i;

	/* If this node is not paged in, return immediately. */
	if ((N->type == NODE_TYPE_NP) || (N->type == NODE_TYPE_READ))
		return;

	/* Otherwise, pick up a lock in order to keep the node paged in. */
	btree_node_lock(T, N);

	/* Recurse down. */
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++)
			btree_node_pageout_recursive(T, N->v.children[i]);
	}

	/* Evict this node from the pool. */
	pool_rec_free(T->P, N);
	N->pool_cookie = NULL;

	/* Free the node data. */
	freedata(T, N);
}

/**
 * btree_node_dirty(T, N):
 * The node ${N} must have type NODE_TYPE_LEAF or NODE_TYPE_PARENT and must
 * have state NODE_STATE_CLEAN.  For the node ${N} and parents as required,
 * create a new DIRTY node and mark the existing node as SHADOW.  The node
 * ${N} must be locked.  Return the new dirty node.
 */
struct node *
btree_node_dirty(struct btree * T, struct node * N)
{
	struct node * N_dirty;
	size_t i;

	/* Sanity check the node. */
	assert(node_present(N));
	assert(N->state == NODE_STATE_CLEAN);

	/* Notify the cleaner. */
	btree_cleaning_notify_dirtying(T->cstate, N);

	/* If we are not a root and p_dirty is clean, recurse up the tree. */
	if ((N->root == 0) &&
	    (N->p_dirty->state == NODE_STATE_CLEAN) &&
	    (btree_node_dirty(T, N->p_dirty) == NULL))
		goto err0;

	/* Allocate a new dirty node. */
	if ((N_dirty = btree_node_mknode(T, N->type, N->height, N->nkeys,
	    NULL, NULL, NULL)) == NULL)
		goto err0;

	/* Copy more node data. */
	N_dirty->root = N->root;
	N_dirty->mlen_t = N->mlen_t;
	N_dirty->mlen_n = N->mlen_n;
	N_dirty->p_dirty = N->p_dirty;

	/* The old node is now SHADOW. */
	N->state = NODE_STATE_SHADOW;
	N->p_dirty = NULL;
	btree_node_lock(T, N);

	/* The new node is dirty. */
	N_dirty->oldestncleaf = (uint64_t)(-1);
	N_dirty->p_shadow = NULL;

	/* Leaf or parent? */
	if (N->type == NODE_TYPE_LEAF) {
		/* Duplicate key-value pairs. */
		if (IMALLOC(N_dirty->u.pairs, N->nkeys, struct kvpair_const))
			goto err1;
		memcpy(N_dirty->u.pairs, N->u.pairs,
		    N->nkeys * sizeof(struct kvpair_const));
	} else {
		/* Duplicate keys. */
		if (IMALLOC(N_dirty->u.keys, N->nkeys,
		    const struct kvldskey *))
			goto err1;
		memcpy(N_dirty->u.keys, N->u.keys,
		    N->nkeys * sizeof(const struct kvldskey *));

		/* Copy child vector. */
		if (IMALLOC(N_dirty->v.children, N->nkeys + 1, struct node *))
			goto err2;
		memcpy(N_dirty->v.children, N->v.children,
		    (N->nkeys + 1) * sizeof(struct node *));

		/* Tell children (if any) about their new dirty parent. */
		for (i = 0; i <= N_dirty->nkeys; i++) {
			if (node_hasplock(N_dirty->v.children[i]))
				btree_node_unlock(T, N);
			N_dirty->v.children[i]->p_dirty = N_dirty;
			if (node_hasplock(N_dirty->v.children[i]))
				btree_node_lock(T, N_dirty);
		}
	}

	/* Update the dirty tree structure. */
	if (N_dirty->root == 0) {
		for (i = 0; i <= N_dirty->p_dirty->nkeys; i++)
			if (N_dirty->p_dirty->v.children[i] == N)
				N_dirty->p_dirty->v.children[i] = N_dirty;
	} else {
		btree_node_unlock(T, T->root_dirty);
		T->root_dirty = N_dirty;
		btree_node_lock(T, T->root_dirty);
	}

	/* Success! */
	return (N_dirty);

err2:
	free(N_dirty->u.keys);
err1:
	pool_rec_free(T->P, N_dirty);
	node_free(N_dirty);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * btree_node_descend(T, N, callback, cookie):
 * If the node ${N} is not present, fetch it.  When it is present (whether it
 * needed to be fetched or not) invoke ${callback}(${cookie}, ${N}) with the
 * node ${N} locked.
 */
int
btree_node_descend(struct btree * T, struct node * N,
    int (* callback)(void *, struct node *), void * cookie)
{
	struct descend * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct descend))) == NULL)
		goto err0;
	C->callback = callback;
	C->cookie = cookie;
	C->N = N;

	/* Fetch the node or schedule an immediate callback. */
	if (!node_present(N)) {
		if (btree_node_fetch(T, N, callback_descend, C))
			goto err1;
	} else {
		btree_node_lock(T, N);
		if (!events_immediate_register(callback_descend, C, 0))
			goto err1;
	}

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* Invoke the callback on the provided node. */
static int
callback_descend(void * cookie)
{
	struct descend * C = cookie;
	int rc;

	/* Invoke the callback. */
	rc = (C->callback)(C->cookie, C->N);

	/* Free the cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

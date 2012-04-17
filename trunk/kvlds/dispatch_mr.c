#include <assert.h>
#include <stdlib.h>

#include "events.h"
#include "imalloc.h"
#include "kvldskey.h"
#include "kvpair.h"
#include "mpool.h"
#include "netbuf.h"
#include "proto_kvlds.h"

#include "btree.h"
#include "btree_cleaning.h"
#include "btree_find.h"
#include "btree_mutate.h"
#include "btree_node.h"

#include "dispatch.h"

/* A single request. */
struct req_cookie {
	struct proto_kvlds_request * R;
	struct node * leaf;
	struct batch * batch;
	int opdone;
};

/* State for a batch of modifying requests. */
struct batch {
	int (*callback_done)(void *);
	void * cookie;
	size_t nreqs;
	struct btree * T;
	struct netbuf_write * WQ;
	struct req_cookie ** reqs;
	size_t leavestofind;
	struct node ** dirties;
	size_t ndirty;
};

/* Shadow/dirty node pointer pair. */
struct nodepair {
	struct node * shadow;
	struct node * dirty;
};

MPOOL(reqcookie, struct req_cookie, 4096);

static int callback_gotleaf(void *, struct node *);
static int callback_gotleaves(void *);
static int callback_balanced(void *);
static int callback_synced(void *);

/* Compare the shadow node pointers. */
static int
compar_snp(const void * _x, const void * _y)
{
	const struct nodepair * x = _x;
	const struct nodepair * y = _y;

	if (x->shadow < y->shadow)
		return (-1);
	else if (x->shadow == y->shadow)
		return (0);
	else
		return (1);
}

/* Find the dirty node corresponding to a shadow node. */
static struct node *
finddirty(struct nodepair * V, size_t N, struct node * shadow)
{
	struct nodepair * pair;
	struct nodepair k;

	/* Binary search. */
	k.shadow = shadow;
	pair = bsearch(&k, V, N, sizeof(struct nodepair), compar_snp);

	/* If we found the shadow node, return the matching dirty node. */
	if (pair != NULL)
		return (pair->dirty);

	/* Otherwise, return the shadow node we were given. */
	return (shadow);
}

/**
 * dispatch_mr_launch(T, reqs, nreqs, WQ, callback_done, cookie):
 * Perform the ${nreqs} modifying requests ${reqs}[] on the B+Tree ${T};
 * write response packets to the write queue ${WQ}; and free the requests and
 * request array.  Invoke the callback ${callback_done}(${cookie}) after the
 * requests have been serviced.
 */
int
dispatch_mr_launch(struct btree * T, struct proto_kvlds_request ** reqs,
    size_t nreqs, struct netbuf_write * WQ,
    int (* callback_done)(void *), void * cookie)
{
	struct batch * B;
	size_t i;

#ifdef SANITY_CHECKS
	/* Sanity check the B+Tree. */
	btree_sanity(T);
#endif

	/* Bake a cookie. */
	if ((B = malloc(sizeof(struct batch))) == NULL)
		goto err0;
	B->callback_done = callback_done;
	B->cookie = cookie;
	B->nreqs = nreqs;
	B->T = T;
	B->WQ = WQ;

	/* Allocate an array of request cookie pointers. */
	if (IMALLOC(B->reqs, B->nreqs, struct req_cookie *))
		goto err1;

	/* Bake request cookies. */
	for (i = 0; i < B->nreqs; i++)
		B->reqs[i] = NULL;
	for (i = 0; i < B->nreqs; i++) {
		if ((B->reqs[i] = mpool_reqcookie_malloc()) == NULL)
			goto err2;
		B->reqs[i]->R = reqs[i];
		B->reqs[i]->batch = B;
		B->reqs[i]->opdone = 0;
	}

	/* If we don't need to find any leaves, schedule the next step. */
	if ((B->leavestofind = B->nreqs) == 0) {
		if (!events_immediate_register(callback_gotleaves, B, 1))
			goto err2;
	}

	/* Look for the leaves. */
	for (i = 0; i < B->nreqs; i++) {
		if (btree_find_leaf(B->T, B->T->root_dirty,
		    B->reqs[i]->R->key, callback_gotleaf, B->reqs[i])) {
			/*
			 * We can't clean up properly since we can't cancel
			 * any already-in-progress leaf-finding; just error
			 * out without cleaning up.
			 */
			goto err0;
		}
	}

	/* Free input request vector. */
	free(reqs);

	/* Success! */
	return (0);

err2:
	for (i = 0; i < B->nreqs; i++)
		mpool_reqcookie_free(B->reqs[i]);
	free(B->reqs);
err1:
	free(B);
err0:
	/* Failure! */
	return (-1);
}

/* We have found the leaf to which a request is attached. */
static int
callback_gotleaf(void * cookie, struct node * N)
{
	struct req_cookie * req = cookie;
	struct batch * B = req->batch;

	/* Record the leaf node. */
	req->leaf = N;

	/* We've found a leaf. */
	B->leavestofind -= 1;

	/* If we've found all of them, move on to the next step. */
	if (B->leavestofind == 0) {
		if (!events_immediate_register(callback_gotleaves, B, 1))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Dirty leaves which will be modified in batch_run. */
static int
batch_dirty(struct batch * B)
{
	struct req_cookie * req;
	struct proto_kvlds_request * R;
	struct nodepair * shadowdirty;
	size_t Nsd;
	size_t i;
	struct kvpair_const * kv;

	/* Allocate array to hold (shadow node, dirty node) pairs. */
	if (IMALLOC(shadowdirty, B->nreqs, struct nodepair))
		goto err0;
	Nsd = 0;

	/* Dirty leaves which will need to be modified. */
	for (i = 0; i < B->nreqs; i++) {
		req = B->reqs[i];
		R = req->R;

		/* If the node has already been dirtied, move on. */
		if (req->leaf->state != NODE_STATE_CLEAN)
			continue;

		/* Look for the relevant key within the node. */
		kv = btree_find_kvpair(req->leaf, R->key);

		/* If this request doesn't do anything, move on. */
		switch (R->type) {
		case PROTO_KVLDS_SET:
			/*
			 * Operation has effect if the key doesn't exist OR
			 * if the value is different; we don't bother
			 * optimizing this case.
			 */
			break;
		case PROTO_KVLDS_ADD:
			/* Operation only has effect if key doesn't exist. */
			if (kv != NULL)
				continue;
			break;
		case PROTO_KVLDS_MODIFY:
		case PROTO_KVLDS_DELETE:
			/* Operation only has effect if key exists. */
			if (kv == NULL)
				continue;
			break;
		case PROTO_KVLDS_CAS:
		case PROTO_KVLDS_CAD:
			/*
			 * Operation only has effect if key exists and is
			 * associated with the right value.
			 */
			if (kv == NULL)
				continue;
			if (kvldskey_cmp(R->oval, kv->v))
				continue;
			break;
		}

		/* Dirty the node. */
		shadowdirty[Nsd].shadow = req->leaf;
		if ((shadowdirty[Nsd].dirty =
		    btree_node_dirty(B->T, req->leaf)) == NULL)
			goto err1;
		Nsd += 1;
	}

	/* Release locks on the clean or shadow nodes. */
	for (i = 0; i < B->nreqs; i++)
		btree_node_unlock(B->T, B->reqs[i]->leaf);

	/* Sort the shadow/dirty pairs if we have any. */
	if (Nsd > 0)
		qsort(shadowdirty, Nsd, sizeof(struct nodepair), compar_snp);

	/* Translate shadow node pointers to dirty node pointers. */
	for (i = 0; i < B->nreqs; i++)
		B->reqs[i]->leaf =
		    finddirty(shadowdirty, Nsd, B->reqs[i]->leaf);

	/* Create a list of dirty leaves for future reference. */
	B->ndirty = Nsd;
	if (IMALLOC(B->dirties, B->ndirty, struct node *))
		goto err1;
	for (i = 0; i < B->ndirty; i++)
		B->dirties[i] = shadowdirty[i].dirty;

	/* Free the shadow/dirty pairs. */
	free(shadowdirty);

	/* Tell the cleaner to dirty nodes now if it wants. */
	if (btree_cleaning_clean(B->T->cstate))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(shadowdirty);
err0:
	/* Failure! */
	return (-1);
}

/* Operation types used in batch_run. */
#define OP_NONE		-1
#define OP_ADD		0
#define OP_MODIFY	1
#define OP_DELETE	2

/* Perform the requested operations. */
static int
batch_run(struct batch * B)
{
	struct req_cookie * req;
	struct proto_kvlds_request * R;
	struct node * leaf;
	size_t i;
	struct kvpair_const * pos;
	const struct kvldskey * val;
	int op;

	/* Prepare leaves for mutation. */
	for (i = 0; i < B->ndirty; i++)
		if (btree_mutate_mutable(B->dirties[i]))
			goto err0;

	/* Handle requests in order. */
	for (i = 0; i < B->nreqs; i++) {
		req = B->reqs[i];
		R = req->R;
		leaf = req->leaf;

		/* If this node isn't dirty, we're not doing anything. */
		if (leaf->state != NODE_STATE_DIRTY)
			continue;

		/* Look for the relevant key within the node. */
		pos = btree_mutate_find(leaf, R->key);
		val = pos->v;

		/* Figure out what we need to do (if anything). */
		op = OP_NONE;
		switch (R->type) {
		case PROTO_KVLDS_SET:
			/* Add or modify as required. */
			op = OP_ADD;
			break;
		case PROTO_KVLDS_CAS:
			/*
			 * Operation only has effect if key exists and is
			 * associated with the right value.
			 */
			if ((val != NULL) &&
			    (kvldskey_cmp(R->oval, val) == 0))
				op = OP_MODIFY;
			break;
		case PROTO_KVLDS_ADD:
			/* Operation only has effect if key doesn't exist. */
			if (val == NULL)
				op = OP_ADD;
			break;
		case PROTO_KVLDS_MODIFY:
			/* Operation only has effect if key exists. */
			if (val != NULL)
				op = OP_MODIFY;
			break;
		case PROTO_KVLDS_DELETE:
			/* Operation only has effect if key exists. */
			if (val != NULL)
				op = OP_DELETE;
			break;
		case PROTO_KVLDS_CAD:
			/*
			 * Operation only has effect if key exists and is
			 * associated with the right value.
			 */
			if ((val != NULL) &&
			    (kvldskey_cmp(R->oval, val) == 0))
				op = OP_DELETE;
			break;
		}

		/* Actually perform the operation (if required). */
		switch (op) {
		case OP_ADD:
			/*
			 * If the key is not present, add the pair.  (Note
			 * that the key might be present even if there is no
			 * value associated with it, since at this point a
			 * deleted key-value pair is represented as a value
			 * of NULL.)
			 */
			if (pos->k == NULL) {
				if (btree_mutate_add(leaf, pos,
				    R->key, R->value))
					goto err0;
				break;
			}

			/* Otherwise, fall through to the MODIFY path. */
			/* FALLTHROUGH. */
		case OP_MODIFY:
			/* Modify the key. */
			pos->v = R->value;
			break;
		case OP_DELETE:
			/* Delete the key. */
			pos->v = NULL;
			break;
		}

		/* Record if we did something. */
		if (op != OP_NONE)
			req->opdone = 1;
	}

	/* We're not going to mutate leaves any more. */
	for (i = 0; i < B->ndirty; i++)
		if (btree_mutate_immutable(B->dirties[i]))
			goto err0;
	free(B->dirties);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* We have all the leaves.  Dirty nodes, unlock the shadows, and modify. */
static int
callback_gotleaves(void * cookie)
{
	struct batch * B = cookie;

	/* Dirty the nodes we will need to modify. */
	if (batch_dirty(B))
		goto err0;

	/*
	 * If we didn't dirty anything, skip the operation-performing,
	 * balancing, and syncing, and go straight to sending responses.
	 */
	if (B->T->root_dirty->state == NODE_STATE_CLEAN)
		goto dosync;

	/* Perform the requested operations. */
	if (batch_run(B))
		goto err0;

	/* Next we need to rebalance the tree. */
	if (btree_balance(B->T, callback_balanced, B))
		goto err0;

	/* Success! */
	return (0);

dosync:
	/* We're skipping balancing and syncing because nothing changed. */
	if (!events_immediate_register(callback_synced, B, 0))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* The tree has been rebalanced.  Flush dirty nodes out. */
static int
callback_balanced(void * cookie)
{
	struct batch * B = cookie;

	/* Fill in matching-prefix values. */
	btree_mlen(B->T);

	/* Sync modified nodes out to durable storage. */
	return (btree_sync(B->T, callback_synced, B));
}

/* Dirty nodes have been flushed out.  Do callbacks and clean up. */
static int
callback_synced(void * cookie)
{
	struct batch * B = cookie;
	struct req_cookie * req;
	struct proto_kvlds_request * R;
	size_t i;

	/* Send response packets. */
	for (i = 0; i < B->nreqs; i++) {
		req = B->reqs[i];
		R = req->R;

		switch (R->type) {
		case PROTO_KVLDS_SET:
			if (proto_kvlds_response_set(B->WQ, R->ID))
				goto err0;
			break;
		case PROTO_KVLDS_CAS:
			if (proto_kvlds_response_cas(B->WQ, R->ID,
			    req->opdone))
				goto err0;
			break;
		case PROTO_KVLDS_ADD:
			if (proto_kvlds_response_add(B->WQ, R->ID,
			    req->opdone))
				goto err0;
			break;
		case PROTO_KVLDS_MODIFY:
			if (proto_kvlds_response_modify(B->WQ, R->ID,
			    req->opdone))
				goto err0;
			break;
		case PROTO_KVLDS_DELETE:
			if (proto_kvlds_response_delete(B->WQ, R->ID))
				goto err0;
			break;
		case PROTO_KVLDS_CAD:
			if (proto_kvlds_response_cad(B->WQ, R->ID,
			    req->opdone))
				goto err0;
			break;
		}
	}

	/* Schedule completion callback. */
	if (!events_immediate_register(B->callback_done, B->cookie, 0))
		goto err0;

	/* Clean up requests and request cookies. */
	for (i = 0; i < B->nreqs; i++) {
		proto_kvlds_request_free(B->reqs[i]->R);
		mpool_reqcookie_free(B->reqs[i]);
	}

	/* Free batch cookie. */
	free(B->reqs);
	free(B);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

#include <assert.h>
#include <stdlib.h>

#include "events.h"
#include "imalloc.h"
#include "kvpair.h"
#include "ptrheap.h"
#include "netbuf.h"
#include "proto_kvlds.h"

#include "btree.h"
#include "btree_find.h"
#include "btree_node.h"

#include "dispatch.h"

/* Non-modifying request state. */
struct nmr_cookie {
	/* State provided by caller. */
	int (*callback_done)(void *);
	void * cookie_done;
	struct btree * T;
	struct proto_kvlds_request * R;
	struct netbuf_write * WQ;

	/* Internal state used for RANGE requests. */
	struct ptrheap * H;
	struct kvldskey * end;
	size_t mlen;
	size_t nkeys;
	size_t rlen;
	size_t leavesleft;
};

static int callback_get_gotleaf(void *, struct node *);
static int callback_range_gotnode(void *, struct node *, struct kvldskey *);
static int callback_range_gotleaf(void *, struct node *);
static int rangedone(struct nmr_cookie *);

/**
 * dispatch_nmr_launch(T, R, WQ, callback_done, cookie_done):
 * Perform non-modifying request ${R} on the B+Tree ${T}; write a response
 * packet to the write queue ${WQ}; and free the requests.  Invoke the
 * callback ${callback_done}(${cookie_done}) after the request is processed.
 */
int
dispatch_nmr_launch(struct btree * T, struct proto_kvlds_request * R,
    struct netbuf_write * WQ,
    int (* callback_done)(void *), void * cookie_done)
{
	struct nmr_cookie * C;

	/* Sanity-check: The NMR type must be reasonable. */
	assert((R->type == PROTO_KVLDS_GET) ||
	    (R->type == PROTO_KVLDS_RANGE));

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct nmr_cookie))) == NULL)
		goto err0;
	C->callback_done = callback_done;
	C->cookie_done = cookie_done;
	C->T = T;
	C->R = R;
	C->WQ = WQ;

	/* Different NMRs need different handling. */
	switch (R->type) {
	case PROTO_KVLDS_GET:
		/* Find the node containing (or not) this key. */
		if (btree_find_leaf(C->T, C->T->root_shadow, C->R->key,
		    callback_get_gotleaf, C))
			goto err1;
		break;
	case PROTO_KVLDS_RANGE:
		/*
		 * Find a node of height 1 or less which is responsible for a
		 * range containing the start key.
		 */
		if (btree_find_range(C->T, C->T->root_shadow,
		    C->R->range_start, 1, callback_range_gotnode, C))
			goto err1;
		break;
	}

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* We've got the leaf node.  Now find the key and send a response. */
static int
callback_get_gotleaf(void * cookie, struct node * N)
{
	struct nmr_cookie * C = cookie;
	struct kvpair_const * kv;

	/* Find the key in this node. */
	kv = btree_find_kvpair(N, C->R->key);

	/* Send the response. */
	if (kv != NULL) {
		/* Send the requested value back to the client. */
		if (proto_kvlds_response_get(C->WQ, C->R->ID, 0,
		    kv->v))
			goto err1;
	} else {
		/* Send a non-present response back to the client. */
		if (proto_kvlds_response_get(C->WQ, C->R->ID, 1,
		    NULL))
			goto err1;
	}

	/* Schedule the request-done callback. */
	if (!events_immediate_register(C->callback_done, C->cookie_done, 0))
		goto err1;

	/* Unlock the node. */
	btree_node_unlock(C->T, N);

	/* Free the request. */
	proto_kvlds_request_free(C->R);

	/* Free the cookie. */
	free(C);

	/* Success! */
	return (0);

err1:
	btree_node_unlock(C->T, N);
	proto_kvlds_request_free(C->R);
	free(C);

	/* Failure! */
	return (-1);
}

/* We've found a node responsible for this range. */
static int
callback_range_gotnode(void * cookie, struct node * N,
    struct kvldskey * end)
{
	struct nmr_cookie * C = cookie;
	size_t start;
	size_t stop;
	size_t i;

	/* Record the end-of-range key. */
	C->end = end;

	/* Record how far all keys in the range must match. */
	C->mlen = N->mlen_t;

	/* Create a heap for holding key-value pairs. */
	if ((C->H = ptrheap_init(kvpair_cmp, NULL, (void *)&C->mlen)) == NULL)
		goto err1;

	/* We don't have any key-value pairs yet. */
	C->nkeys = 0;
	C->rlen = 0;

	/* Leaves and parents get handled differently. */
	switch (N->height) {
	case 0:
		/* Descend into a single leaf. */
		C->leavesleft = 1;
		if (btree_node_descend(C->T, N, callback_range_gotleaf, C))
			goto err2;
		break;
	case 1:
		/* Not descending into any leaves yet. */
		C->leavesleft = 0;

		/* Figure out which leaf to start with. */
		start = btree_find_child(N, C->R->range_start);

		/* Figure out the maximum number of leaves to process. */
		stop = start + C->R->range_max / C->T->pagelen;
		if (stop == start)
			stop = start + 1;

		/* Process leaf nodes. */
		for (i = start; (i <= N->nkeys) && (i < stop); i++) {
			/* Do this leaf. */
			C->leavesleft += 1;
			if (btree_node_descend(C->T, N->v.children[i],
			    callback_range_gotleaf, C))
				goto err0;

			/* Stop if we've gone too far. */
			if ((i < N->nkeys) && (kvldskey_cmp(C->R->range_end,
			    N->u.keys[i]) < 0)) {
				i++;
				break;
			}
		}

		/* Adjust our end pointer if we didn't do all the leaves. */
		if (i < N->nkeys + 1) {
			kvldskey_free(C->end);
			if ((C->end = kvldskey_dup(N->u.keys[i - 1])) == NULL)
				goto err0;
		}
		break;
	}

	/* Release the lock picked up by btree_find_range. */
	btree_node_unlock(C->T, N);

	/* Success! */
	return (0);

err2:
	ptrheap_free(C->H);
err1:
	kvldskey_free(C->end);
	proto_kvlds_request_free(C->R);
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* Suck key-value pairs from a leaf into the heap. */
static int
callback_range_gotleaf(void * cookie, struct node * N)
{
	struct nmr_cookie * C = cookie;
	struct kvpair * kv;
	size_t i;

	/* Scan through the key-value pairs (maybe) copying them. */
	for (i = 0; i < N->nkeys; i++) {
		/* Is this key too small? */
		if (kvldskey_cmp(N->u.pairs[i].k, C->R->range_start) < 0)
			continue;

		/* Is this key too large? */
		if ((C->R->range_end->len > 0) &&
		    (kvldskey_cmp(N->u.pairs[i].k, C->R->range_end) > 0))
			continue;

		/* Does it fit? */
		C->rlen += kvldskey_serial_size(N->u.pairs[i].k);
		C->rlen += kvldskey_serial_size(N->u.pairs[i].v);
		if ((C->nkeys > 0) && (C->R->range_max < C->rlen))
			break;

		/* Add the pair to the heap. */
		if ((kv = malloc(sizeof(struct kvpair))) == NULL)
			goto err0;
		if ((kv->k = kvldskey_dup(N->u.pairs[i].k)) == NULL)
			goto err1;
		if ((kv->v = kvldskey_dup(N->u.pairs[i].v)) == NULL)
			goto err2;
		if (ptrheap_add(C->H, kv))
			goto err3;
		C->nkeys += 1;
	}

	/* If we exited early, adjust the end pointer. */
	if (i < N->nkeys) {
		kvldskey_free(C->end);
		if ((C->end = kvldskey_dup(N->u.pairs[i].k)) == NULL)
			goto err0;
	}

	/* Release the lock picked up by btree_node_descend. */
	btree_node_unlock(C->T, N);

	/* We've handled a leaf. */
	C->leavesleft -= 1;

	/* Are we done all the leaves? */
	if (C->leavesleft == 0) {
		if (rangedone(C))
			goto err0;
	}

	/* Success! */
	return (0);

err3:
	kvldskey_free(kv->v);
err2:
	kvldskey_free(kv->k);
err1:
	free(kv);
err0:
	/* Failure! */
	return (-1);
}

/* Send the RANGE response and clean up. */
static int
rangedone(struct nmr_cookie * C)
{
	const struct kvldskey * next;
	struct kvldskey ** keys;
	struct kvldskey ** values;
	struct kvpair * kv;
	size_t i;

	/*
	 * If we've handled a range which goes beyond the ending key we were
	 * provided with, we want to return the ending key as the next key.
	 */
	if (C->end->len == 0)
		next = C->R->range_end;
	else if (C->R->range_end->len == 0)
		next = C->end;
	else if (kvldskey_cmp(C->end, C->R->range_end) < 0)
		next = C->end;
	else
		next = C->R->range_end;

	/* Allocate arrays for holding keys and values. */
	if (IMALLOC(keys, C->nkeys, struct kvldskey *))
		goto err2;
	if (IMALLOC(values, C->nkeys, struct kvldskey *))
		goto err3;

	/* Pull key-value pairs out of the heap. */
	for (i = 0; i < C->nkeys; i++) {
		/* Get a key-value pair. */
		kv = ptrheap_getmin(C->H);
		assert(kv != NULL);

		/* Remove it from the heap. */
		ptrheap_deletemin(C->H);

		/* Stuff the key and value into the respective arrays. */
		keys[i] = kv->k;
		values[i] = kv->v;

		/* Free the key-value pair structure. */
		free(kv);
	}

	/* Send the RANGE response. */
	if (proto_kvlds_response_range(C->WQ, C->R->ID, C->nkeys, next,
	    keys, values))
		goto err4;

	/* Free the values. */
	for (i = 0; i < C->nkeys; i++)
		kvldskey_free(values[i]);
	free(values);

	/* Free the keys. */
	for (i = 0; i < C->nkeys; i++)
		kvldskey_free(keys[i]);
	free(keys);

	/* Free the heap. */
	ptrheap_free(C->H);

	/* Free the end-of-range value provided by btree_find_range. */
	kvldskey_free(C->end);

	/* Free the RANGE request. */
	proto_kvlds_request_free(C->R);

	/* Schedule the completion callback. */
	if (!events_immediate_register(C->callback_done, C->cookie_done, 0))
		goto err1;

	/* Free the cookie. */
	free(C);

	/* Success! */
	return (0);

err4:
	for (i = 0; i < C->nkeys; i++)
		kvldskey_free(values[i]);
	for (i = 0; i < C->nkeys; i++)
		kvldskey_free(keys[i]);
	free(values);
err3:
	free(keys);
err2:
	ptrheap_free(C->H);
	kvldskey_free(C->end);
	proto_kvlds_request_free(C->R);
err1:
	free(C);

	/* Failure! */
	return (-1);
}

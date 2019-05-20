#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "imalloc.h"
#include "kvldskey.h"
#include "kvpair.h"

#include "btree.h"
#include "node.h"
#include "serialize.h"

#include "btree_node.h"

/* Return the number of parts into which a leaf node should be split. */
static size_t
nparts_leaf(struct node * N, size_t breakat)
{
	size_t nparts;
	size_t i;
	size_t cursize;

	/* This is a leaf. */
	assert(N->type == NODE_TYPE_LEAF);

	/* Scan through nodes. */
	nparts = 1;
	cursize = SERIALIZE_OVERHEAD;
	for (i = 0; i < N->nkeys; i++) {
		/* Should we split before this next key-value pair? */
		if (cursize > breakat) {
			nparts += 1;
			cursize = SERIALIZE_OVERHEAD;
		}

		/* Add the key size. */
		cursize += kvldskey_serial_size(N->u.pairs[i].k);

		/* Add the value size. */
		cursize += kvldskey_serial_size(N->u.pairs[i].v);
	}

	/* Return the number of parts. */
	return (nparts);
}

/* Return the number of parts into which a parent node should be split. */
static size_t
nparts_parent(struct node * N, size_t breakat)
{
	size_t nparts;
	size_t i;
	size_t cursize;

	/* This is a parent. */
	assert(N->type == NODE_TYPE_PARENT);

	/* Scan through nodes. */
	nparts = 1;
	cursize = SERIALIZE_OVERHEAD + SERIALIZE_PERCHILD;
	for (i = 1; i <= N->nkeys; i++) {
		/* Should we split before this next child? */
		if (cursize > breakat) {
			nparts += 1;
			cursize = SERIALIZE_OVERHEAD + SERIALIZE_PERCHILD;
		} else {
			/* Add the separator key size. */
			cursize += kvldskey_serial_size(N->u.keys[i-1]);

			/* Add the next child. */
			cursize += SERIALIZE_PERCHILD;
		}
	}

	/* Return the number of parts. */
	return (nparts);
}

/**
 * btree_node_split_nparts(T, N):
 * Return the number of nodes into which the node ${N} belonging to the
 * B+Tree ${T} will be split by ntree_node_split.
 */
size_t
btree_node_split_nparts(struct btree * T, struct node * N)
{
	size_t breakat;

	/* We will split when we exceed 2/3 of a full node. */
	breakat = (T->pagelen * 2)/3;

	/* Handle leaves and parents separately. */
	if (N->type == NODE_TYPE_LEAF)
		return (nparts_leaf(N, breakat));
	else
		return (nparts_parent(N, breakat));
}

/* Make a leaf.  Copy pointers to keys. */
static struct node *
makeleaf(struct btree * T, size_t nkeys, struct kvpair_const * pairs)
{
	struct kvpair_const * new_pairs;
	struct node * N;

	/* Allocate new array of pairs and copy keys and values. */
	if (IMALLOC(new_pairs, nkeys, struct kvpair_const))
		goto err0;
	memcpy(new_pairs, pairs, nkeys * sizeof(struct kvpair_const));

	/* Construct a leaf node. */
	if ((N = btree_node_mkleaf(T, nkeys, new_pairs)) == NULL)
		goto err1;

	/* Success! */
	return (N);

err1:
	free(new_pairs);
err0:
	/* Failure! */
	return (NULL);
}

/* Split a leaf. */
static int
split_leaf(struct btree * T, struct node * N, const struct kvldskey ** keys,
    struct node ** parents, size_t * nparts, size_t breakat)
{
	size_t i;
	size_t cursize;
	size_t nkeys;

	/* This is a leaf. */
	assert(N->type == NODE_TYPE_LEAF);

	/* Scan through nodes. */
	*nparts = 0;
	cursize = SERIALIZE_OVERHEAD;
	nkeys = 0;
	for (i = 0; i < N->nkeys; i++) {
		/* Should we split before this next key-value pair? */
		if (cursize > breakat) {
			/* Create a new leaf node. */
			if ((parents[*nparts] =
			    makeleaf(T, nkeys, &N->u.pairs[i - nkeys])) == NULL)
				goto err1;

			/*
			 * Create a separator key which is greater than the
			 * previous key and less than or equal to the next
			 * key.
			 */
			keys[*nparts] = N->u.pairs[i].k;

			/* We've finished this part. */
			*nparts += 1;
			cursize = SERIALIZE_OVERHEAD;
			nkeys = 0;
		}

		/* Add the key size. */
		cursize += kvldskey_serial_size(N->u.pairs[i].k);

		/* Add the value size. */
		cursize += kvldskey_serial_size(N->u.pairs[i].v);

		/* We have a key in the node we're constructing. */
		nkeys += 1;
	}

	/* Create a leaf with whatever we've got left over. */
	if ((parents[*nparts] = makeleaf(T, nkeys,
	    &N->u.pairs[i - nkeys])) == NULL)
		goto err1;
	*nparts += 1;

	/* Destroy the old node. */
	btree_node_destroy(T, N);

	/* Success! */
	return (0);

err1:
	for (; *nparts > 0; *nparts -= 1)
		btree_node_destroy(T, parents[*nparts - 1]);

	/* Failure! */
	return (-1);
}

/* Free a parent node but not its separator keys or children. */
static void
destroy_parent_nokeys(struct btree * T, struct node * N)
{

	/* Detach the keys from the node. */
	free(N->u.keys);

	/* Detach the children from the node. */
	free(N->v.children);

	/* This node has no data to free. */
	N->nkeys = (size_t)(-1);

	/* Free the node. */
	btree_node_destroy(T, N);
}

/* Make a parent.  Copy pointers to keys and children. */
static struct node *
makeparent(struct btree * T, int height, size_t nkeys,
    const struct kvldskey ** keys, struct node ** children)
{
	const struct kvldskey ** new_keys;
	struct node ** new_children;
	struct node * N;

	/* Allocate new key array and copy pointers to keys. */
	if (IMALLOC(new_keys, nkeys, const struct kvldskey *))
		goto err0;
	memcpy(new_keys, keys, nkeys * sizeof(const struct kvldskey *));

	/* Allocate new child array and copy pointers to children. */
	if (IMALLOC(new_children, nkeys + 1, struct node *))
		goto err1;
	memcpy(new_children, children, (nkeys + 1) * sizeof(struct node *));

	/* Construct a parent node. */
	if ((N = btree_node_mkparent(T, height,
	    nkeys, new_keys, new_children)) == NULL)
		goto err2;

	/* Success! */
	return (N);

err2:
	free(new_children);
err1:
	free(new_keys);
err0:
	/* Failure! */
	return (NULL);
}

/* Split a parent. */
static int
split_parent(struct btree * T, struct node * N, const struct kvldskey ** keys,
    struct node ** parents, size_t * nparts, size_t breakat)
{
	size_t i, j;
	size_t cursize;
	size_t nkeys;

	/* This is a parent. */
	assert(N->type == NODE_TYPE_PARENT);

	/* Scan through nodes. */
	*nparts = 0;
	cursize = SERIALIZE_OVERHEAD + SERIALIZE_PERCHILD;
	nkeys = 0;
	for (i = 1; i <= N->nkeys; i++) {
		/* Should we split before this next child? */
		if (cursize > breakat) {
			/* Create a new parent node. */
			if ((parents[*nparts] = makeparent(T, N->height,
			    nkeys, &N->u.keys[i - nkeys - 1],
			    &N->v.children[i - nkeys - 1])) == NULL)
				goto err1;

			/* Grab the separator key. */
			keys[*nparts] = N->u.keys[i - 1];

			/* We've finished this part. */
			*nparts += 1;
			cursize = SERIALIZE_OVERHEAD + SERIALIZE_PERCHILD;
			nkeys = 0;
		} else {
			/* Add the separator key size. */
			cursize += kvldskey_serial_size(N->u.keys[i-1]);
			nkeys += 1;

			/* Add the next child. */
			cursize += SERIALIZE_PERCHILD;
		}
	}

	/* Create a parent node with whatever we've got left over. */
	if ((parents[*nparts] = makeparent(T, N->height, nkeys,
	    &N->u.keys[i - nkeys - 1], &N->v.children[i - nkeys - 1])) == NULL)
		goto err1;
	*nparts += 1;

	/* Adjust parentage of children. */
	for (i = 0; i < *nparts; i++) {
		for (j = 0; j <= parents[i]->nkeys; j++) {
			/* Unlock old parent. */
			if (node_hasplock(parents[i]->v.children[j]))
				btree_node_unlock(T, N);

			/* Set dirty parent pointer. */
			parents[i]->v.children[j]->p_dirty = parents[i];

			/* Lock new parent. */
			if (node_hasplock(parents[i]->v.children[j]))
				btree_node_lock(T, parents[i]);
		}
	}

	/* Destroy the old node, but not its children. */
	destroy_parent_nokeys(T, N);

	/* Success! */
	return (0);

err1:
	for (; *nparts > 0; *nparts -= 1)
		destroy_parent_nokeys(T, parents[*nparts - 1]);

	/* Failure! */
	return (-1);
}

/**
 * btree_node_split(T, N, keys, parents, nparts):
 * Split the node ${N} belonging to the B+Tree ${T} into parts which are
 * small enough to be serialized to a single page.  Write the resulting
 * nodes into ${parents} and the separating keys into ${keys}; write the
 * number of parts into ${nparts} (this value must match the value returned
 * by btree_node_split_nparts).  Free the node ${N}.  On failure, return
 * with ${N} unmodified.
 */
int
btree_node_split(struct btree * T, struct node * N,
    const struct kvldskey ** keys, struct node ** parents, size_t * nparts)
{
	size_t breakat;
	int rc;

	/* Sanity-check: We should never try to split a non-dirty node. */
	assert(N->state == NODE_STATE_DIRTY);

	/* We will split when we exceed 2/3 of a full node. */
	breakat = (T->pagelen * 2)/3;

	/* Handle leaves and parents separately. */
	if (N->type == NODE_TYPE_LEAF)
		rc = split_leaf(T, N, keys, parents, nparts, breakat);
	else
		rc = split_parent(T, N, keys, parents, nparts, breakat);

	/* On success, update recorded size of tree. */
	if (rc == 0)
		T->nnodes += *nparts - 1;

	/* Return status from split_leaf or split_parent. */
	return (rc);
}

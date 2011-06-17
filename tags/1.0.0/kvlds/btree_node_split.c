#include <assert.h>
#include <stdlib.h>

#include "kvldskey.h"
#include "kvpair.h"
#include "imalloc.h"

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

/* Make a leaf. */
static struct node *
makeleaf(struct btree * T, size_t nkeys, struct kvpair * pairs)
{
	struct kvpair * new_pairs;
	struct node * N;
	size_t i;

	/* Allocate new array of pairs. */
	if (IMALLOC(new_pairs, nkeys, struct kvpair))
		goto err0;

	/* Duplicate keys and values. */
	for (i = 0; i < nkeys; i++) {
		new_pairs[i].k = pairs[i].k;
		new_pairs[i].v = pairs[i].v;
		kvldskey_ref(new_pairs[i].k);
		kvldskey_ref(new_pairs[i].v);
	}

	/* Construct a leaf node. */
	if ((N = btree_node_mkleaf(T, nkeys, new_pairs)) == NULL)
		goto err1;

	/* Success! */
	return (N);

err1:
	for (i = 0; i < nkeys; i++) {
		kvldskey_free(new_pairs[i].v);
		kvldskey_free(new_pairs[i].k);
	}
	free(new_pairs);
err0:
	/* Failure! */
	return (NULL);
}

/* Split a leaf. */
static int
split_leaf(struct btree * T, struct node * N, struct kvldskey ** keys,
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
			if ((keys[*nparts] = kvldskey_sep(N->u.pairs[i - 1].k,
			    N->u.pairs[i].k)) == NULL)
				goto err2;

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

err2:
	btree_node_destroy(T, parents[*nparts]);
err1:
	for (; *nparts > 0; *nparts -= 1) {
		kvldskey_free(keys[*nparts - 1]);
		btree_node_destroy(T, parents[*nparts - 1]);
	}

	/* Failure! */
	return (-1);
}

/* Make a parent. */
static struct node *
makeparent(struct btree * T, int height, size_t nkeys,
    struct kvldskey ** keys, struct node ** children)
{
	struct kvldskey ** new_keys;
	struct node ** new_children;
	struct node * N;
	size_t i;

	/* Allocate new key and child arrays. */
	if (IMALLOC(new_keys, nkeys, struct kvldskey *))
		goto err0;
	if (IMALLOC(new_children, nkeys + 1, struct node *))
		goto err1;

	/* Duplicate keys. */
	for (i = 0; i < nkeys; i++) {
		new_keys[i] = keys[i];
		kvldskey_ref(new_keys[i]);
	}

	/* Copy child pointers. */
	for (i = 0; i <= nkeys; i++)
		new_children[i] = children[i];

	/* Construct a parent node. */
	if ((N = btree_node_mkparent(T, height,
	    nkeys, new_keys, new_children)) == NULL)
		goto err2;

	/* Success! */
	return (N);

err2:
	for (i = 0; i < nkeys; i++)
		kvldskey_free(new_keys[i]);
	free(new_children);
err1:
	free(new_keys);
err0:
	/* Failure! */
	return (NULL);
}

/* Split a parent. */
static int
split_parent(struct btree * T, struct node * N, struct kvldskey ** keys,
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

			/* Duplicate the separator key. */
			keys[*nparts] = N->u.keys[i - 1];
			kvldskey_ref(keys[*nparts]);

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
	for (i = 0; i <= N->nkeys; i++)
		N->v.children[i] = NULL;
	btree_node_destroy(T, N);

	/* Success! */
	return (0);

err1:
	for (; *nparts > 0; *nparts -= 1) {
		kvldskey_free(keys[*nparts - 1]);
		for (i = 0; i <= parents[*nparts - 1]->nkeys; i++)
			parents[*nparts - 1]->v.children[i] = NULL;
		btree_node_destroy(T, parents[*nparts - 1]);
	}

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
btree_node_split(struct btree * T, struct node * N, struct kvldskey ** keys,
    struct node ** parents, size_t * nparts)
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

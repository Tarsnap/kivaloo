#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "kvldskey.h"
#include "kvpair.h"

#include "btree.h"
#include "imalloc.h"
#include "node.h"

#include "btree_node.h"

static int
merge_leaf(struct btree * T, struct node ** c_in, struct node ** c_out,
    size_t nsep)
{
	size_t nkeys;
	size_t i, j;
	struct kvpair_const * pairs;
	struct node * N;

	/* The input nodes are dirty leaves. */
	for (i = 0; i <= nsep; i++) {
		assert(c_in[i]->type == NODE_TYPE_LEAF);
		assert(c_in[i]->state == NODE_STATE_DIRTY);
	}

	/* Figure out how big the new node needs to be. */
	for (nkeys = i = 0; i <= nsep; i++)
		nkeys += c_in[i]->nkeys;

	/* Allocate array of pair structures. */
	if (IMALLOC(pairs, nkeys, struct kvpair_const))
		goto err0;

	/* Copy in pointers to keys and values. */
	for (j = i = 0; i <= nsep; j += c_in[i]->nkeys, i++)
		memcpy(&pairs[j], c_in[i]->u.pairs,
		    c_in[i]->nkeys * sizeof(struct kvpair_const));

	/* Create a node. */
	if ((N = btree_node_mkleaf(T, nkeys, pairs)) == NULL)
		goto err1;

	/* Assign a parent to this node. */
	N->p_dirty = c_in[0]->p_dirty;
	btree_node_lock(T, N->p_dirty);

	/* Destroy old nodes. */
	for (i = 0; i <= nsep; i++)
		btree_node_destroy(T, c_in[i]);

	/* Store created node. */
	c_out[0] = N;

	/* Success! */
	return (0);

err1:
	free(pairs);
err0:
	/* Failure! */
	return (-1);
}

static int
merge_parent(struct btree * T, struct node ** c_in,
    const struct kvldskey ** k_in, struct node ** c_out, size_t nsep)
{
	size_t nkeys;
	size_t i, j;
	const struct kvldskey ** keys;
	struct node ** children;
	struct node * N;

	/* The input nodes are dirty parents. */
	for (i = 0; i <= nsep; i++) {
		assert(c_in[i]->type == NODE_TYPE_PARENT);
		assert(c_in[i]->state == NODE_STATE_DIRTY);
	}

	/* Figure out how big the new node needs to be. */
	for (nkeys = i = 0; i <= nsep; i++)
		nkeys += c_in[i]->nkeys + 1;
	nkeys -= 1;

	/* Allocate key and child arrays. */
	if (IMALLOC(keys, nkeys, const struct kvldskey *))
		goto err0;
	if (IMALLOC(children, nkeys + 1, struct node *))
		goto err1;

	/* Copy keys into new array. */
	for (j = i = 0; i < nsep; i++) {
		/* Separator keys within a merging node. */
		memcpy(&keys[j], c_in[i]->u.keys,
		    c_in[i]->nkeys * sizeof(const struct kvldskey *));
		j += c_in[i]->nkeys;

		/* Separator keys between merging nodes. */
		keys[j++] = k_in[i];
	}
	memcpy(&keys[j], c_in[i]->u.keys,
	    c_in[i]->nkeys * sizeof(const struct kvldskey *));

	/* Copy children into new array. */
	for (j = i = 0; i <= nsep; j += c_in[i]->nkeys + 1, i++)
		memcpy(&children[j], c_in[i]->v.children,
		    (c_in[i]->nkeys + 1) * sizeof(struct node *));

	/* Create a node. */
	if ((N = btree_node_mkparent(T, c_in[0]->height,
	    nkeys, keys, children)) == NULL)
		goto err2;

	/* Assign a parent to this node. */
	N->p_dirty = c_in[0]->p_dirty;
	btree_node_lock(T, N->p_dirty);

	/* Adjust parentage of children. */
	for (j = 0; j <= nkeys; j++) {
		/* Unlock old parent. */
		if (node_hasplock(children[j]))
			btree_node_unlock(T, children[j]->p_dirty);

		/* Set new dirty parent. */
		children[j]->p_dirty = N;

		/* Lock new parent. */
		if (node_hasplock(children[j]))
			btree_node_lock(T, children[j]->p_dirty);
	}

	/* Destroy old nodes (but not their children). */
	for (i = 0; i <= nsep; i++) {
		free(c_in[i]->u.keys);
		free(c_in[i]->v.children);
		c_in[i]->nkeys = -1;
		btree_node_destroy(T, c_in[i]);
	}

	/* Store created node. */
	c_out[0] = N;

	/* Success! */
	return (0);

err2:
	free(children);
err1:
	free(keys);
err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_node_merge(T, c_in, k_in, c_out, k_out, nsep):
 * Merge c_in[0 .. nsep] into a single node, and store it as c_out[0].
 * Separator keys, if needed, are in k_in[0 .. nsep - 1].  If the merge
 * fails, copy the (unmodified) nodes c_in[0 .. nsep] to c_out[0 .. nsep]
 * and the separator keys k_in[0 .. nsep - 1] to k_out[0 .. nsep - 1].
 */
int
btree_node_merge(struct btree * T,
    struct node ** c_in, const struct kvldskey ** k_in,
    struct node ** c_out, const struct kvldskey ** k_out, size_t nsep)
{
	size_t i;
	int rc;

	/* Sanity-check: All the nodes should be dirty. */
	for (i = 0; i <= nsep; i++)
		assert(c_in[i]->state == NODE_STATE_DIRTY);

	/* Handle leaves and parents separately. */
	if (c_in[0]->type == NODE_TYPE_LEAF)
		rc = merge_leaf(T, c_in, c_out, nsep);
	else
		rc = merge_parent(T, c_in, k_in, c_out, nsep);

	/* If we succeeded, update the tree size. */
	if (!rc) {
		/* The tree has shrunk. */
		T->nnodes -= nsep;
	}

	/* If we failed, copy the nodes and keys. */
	if (rc) {
		memcpy(c_out, c_in, (nsep + 1) * sizeof(struct node *));
		memcpy(k_out, k_in, nsep * sizeof(const struct kvldskey *));
	}

	/* Return code from merge_(leaf|parent). */
	return (rc);
}

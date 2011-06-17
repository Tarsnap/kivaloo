#include "kvldskey.h"

#include "node.h"

#include "btree.h"

/**
 * mlen_traverse(N, start, end):
 * Set the mlen value for all dirty nodes in the subtree rooted at ${N}; the
 * node ${N} is responsible for the range [${start}, ${end}) where NULL is
 * taken to be the start/end of keyspace.
 */
static void
mlen_traverse(struct node * N, const struct kvldskey * start,
    const struct kvldskey * end)
{
	size_t i;

	/* If this node is not dirty, we have nothing to do. */
	if (N->state != NODE_STATE_DIRTY)
		return;

	/* If this node has children, recurse down. */
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++)
			mlen_traverse(N->v.children[i],
			    (i == 0) ? start : N->u.keys[i - 1],
			    (i < N->nkeys) ? N->u.keys[i] : end);
	}

	/* Store the matching prefix length for this node. */
	if ((start == NULL) || (end == NULL))
		N->mlen = 0;
	else
		N->mlen = kvldskey_mlen(start, end);
}

/**
 * btree_mlen(T):
 * Fill in the matching-prefix-length values in dirty nodes in the tree ${T}.
 */
void
btree_mlen(struct btree * T)
{

	/* Start from the top of the dirty tree and work down. */
	mlen_traverse(T->root_dirty, NULL, NULL);
}

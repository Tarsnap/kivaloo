#include <assert.h>

#include "kvpair.h"
#include "pool.h"

#include "btree_node.h"
#include "node.h"

#include "btree.h"

static void
sanity(struct btree * T, struct node * N, int state)
{
	size_t i;
	size_t nlcks;

	/* NULL isn't a node. */
	assert(N != NULL);

	/* State must be sane. */
	assert((N->state == NODE_STATE_CLEAN) ||
	    (N->state == NODE_STATE_SHADOW) ||
	    (N->state == NODE_STATE_DIRTY));

	/* Type must be sane. */
	assert((N->type == NODE_TYPE_LEAF) ||
	    (N->type == NODE_TYPE_PARENT) ||
	    (N->type == NODE_TYPE_NP) ||
	    (N->type == NODE_TYPE_READ));

	/* State must match parent's state. */
	assert((N->state == state) || (N->state == NODE_STATE_CLEAN));

	/* Roots have no parents. */
	if (N->root)
		assert((N->p_shadow == NULL) && (N->p_dirty == NULL));

	/* Roots must be accessible. */
	if (N->root)
		assert((N == T->root_shadow) || (N == T->root_dirty));

	/* Non-roots must have the right parents. */
	if (N->root == 0) {
		switch (N->state) {
		case NODE_STATE_CLEAN:
			assert((N->p_shadow != NULL) && (N->p_dirty != NULL));
			break;
		case NODE_STATE_SHADOW:
			assert((N->p_shadow != NULL) && (N->p_dirty == NULL));
			break;
		case NODE_STATE_DIRTY:
			assert((N->p_shadow == NULL) && (N->p_dirty != NULL));
			break;
		}
	}

	/* A node is a leaf iff it has height 0. */
	assert((N->type != NODE_TYPE_PARENT) || (N->height != 0));
	assert((N->type != NODE_TYPE_LEAF) || (N->height == 0));
	assert(node_present(N) || (N->height == -1));

	/* Non-present nodes must be clean and non-root. */
	if (!node_present(N)) {
		/* Must be clean. */
		assert(N->state == NODE_STATE_CLEAN);

		/* Must not be a root. */
		assert(N->root == 0);
	}

	/* Keys must be sane. */
	if (N->type == NODE_TYPE_PARENT) {
		assert((N->u.keys != NULL) || (N->nkeys == 0));
		for (i = 0; i < N->nkeys; i++)
			assert(N->u.keys[i] != NULL);
	}

	/* Key-value pairs must be sane. */
	if (N->type == NODE_TYPE_LEAF) {
		assert((N->u.pairs != NULL) || (N->nkeys == 0));
		for (i = 0; i < N->nkeys; i++) {
			assert(N->u.pairs[i].k != NULL);
			assert(N->u.pairs[i].v != NULL);
		}
	}

	/* Parents have sane children of correct height. */
	if (N->type == NODE_TYPE_PARENT) {
		assert(N->v.children != NULL);
		for (i = 0; i <= N->nkeys; i++) {
			if (node_present(N->v.children[i]))
				assert(N->v.children[i]->height == N->height - 1);
			sanity(T, N->v.children[i], N->state);
		}
	}

	/* Check lock count. */
	nlcks = 0;
	if (N == T->root_shadow)
		nlcks += 1;
	if (N == T->root_dirty)
		nlcks += 1;
	if (N->state != NODE_STATE_CLEAN)
		nlcks += 1;
	if (N->type == NODE_TYPE_PARENT) {
		for (i = 0; i <= N->nkeys; i++) {
			if (node_hasplock(N->v.children[i])) {
				if (N->v.children[i]->p_shadow == N)
					nlcks += 1;
				if (N->v.children[i]->p_dirty == N)
					nlcks += 1;
			}
		}
	}
	if ((N->type == NODE_TYPE_LEAF) && (N->state == NODE_STATE_CLEAN) &&
	    (N->v.cstate != NULL))
		nlcks += 1;
	if (N->type == NODE_TYPE_READ)
		nlcks += btree_node_fetch_lockcount(N);
	if (N->type != NODE_TYPE_NP)
		assert(pool_rec_lockcount(T->P, N) == nlcks);
}

/**
 * btree_sanity(T):
 * Perform sanity-checks on the tree ${T}.  This is time consuming (it will
 * touch every paged-in node) and thus only exists for debugging purposes.
 * This function may not be invoked while there are priority-zero immediate
 * callbacks pending.
 */
void
btree_sanity(struct btree * T)
{

	/* If we have a shadow tree, check it. */
	if (T->root_shadow)
		sanity(T, T->root_shadow, NODE_STATE_SHADOW);

	/* Check the dirty tree. */
	sanity(T, T->root_dirty, NODE_STATE_DIRTY);

	/* Sanity-check: A clean root is both dirty and shadow. */
	assert((T->root_dirty->state != NODE_STATE_CLEAN) ||
	    (T->root_dirty == T->root_shadow));
	assert((T->root_shadow == NULL) ||
	    (T->root_shadow->state != NODE_STATE_CLEAN) ||
	    (T->root_shadow == T->root_dirty));
}

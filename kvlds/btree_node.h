#ifndef BTREE_NODE_H_
#define BTREE_NODE_H_

#include <stddef.h>
#include <stdint.h>

#include "pool.h"

#include "btree.h"

/* Opaque types. */
struct kvldskey;
struct kvpair_const;
struct node;

/**
 * btree_node_mknode(T, type, height, nkeys, keys, children, pairs):
 * Create and return a new dirty node with lock count 1 belonging to the
 * B+Tree ${T}, of type ${type}, height ${height}, ${nkeys} keys; if a parent
 * then the keys are in ${keys} and children are in ${children}; if a leaf
 * then the key-value pairs are in ${pairs}.
 */
struct node * btree_node_mknode(struct btree *, unsigned int, int, size_t,
    const struct kvldskey **, struct node **, struct kvpair_const *);
#define btree_node_mkleaf(T, nkeys, pairs)			\
	btree_node_mknode(T, NODE_TYPE_LEAF, 0, nkeys, NULL, NULL, pairs)
#define btree_node_mkparent(T, height, nkeys, keys, children)	\
	btree_node_mknode(T, NODE_TYPE_PARENT,			\
	    height, nkeys, keys, children, NULL)

/**
 * btree_node_lock(T, N):
 * Lock the node ${N}.
 */
static inline void
btree_node_lock(struct btree * T, struct node * N)
{

	/* Lock the node in the pool. */
	if (N != NULL)
		pool_rec_lock(T->P, N);
}

/**
 * btree_node_unlock(T, N):
 * Unlock the node ${N}.
 */
static inline void
btree_node_unlock(struct btree * T, struct node * N)
{

	/* Unlock the node in the pool. */
	if (N != NULL)
		pool_rec_unlock(T->P, N);
}

/**
 * btree_node_fetch(T, N, callback, cookie):
 * Fetch the node ${N} which is currently of type either NODE_TYPE_NP or
 * NODE_TYPE_READ in the B+Tree ${T}.  Invoke ${callback}(${cookie}) when
 * complete, with the node locked.
 */
int btree_node_fetch(struct btree *, struct node *, int (*)(void *), void *);

/**
 * btree_node_fetch_try(T, N, callback, cookie):
 * As btree_node_fetch(), but if the page does not exist the callback will be
 * performed with the node not present.
 */
int btree_node_fetch_try(struct btree *, struct node *, int (*)(void *), void *);

#ifdef SANITY_CHECKS
/**
 * btree_node_fetch_lockcount(N):
 * Return the number of locks held on ${N} by fetch callbacks.
 */
size_t btree_node_fetch_lockcount(struct node *);
#endif

/**
 * btree_node_descend(T, N, callback, cookie):
 * If the node ${N} is not present, fetch it.  When it is present (whether it
 * needed to be fetched or not) invoke ${callback}(${cookie}, ${N}) with the
 * node ${N} locked.
 */
int btree_node_descend(struct btree *, struct node *,
    int (*)(void *, struct node *), void *);

/**
 * btree_node_destroy(T, N):
 * Remove the node ${N} from the B+Tree ${T} and free it.  If present, the
 * node must have lock count 1 and must not be in the process of being
 * fetched.  Note that this function does not remove dangling pointers from
 * any parent(s).
 */
void btree_node_destroy(struct btree *, struct node *);

/**
 * btree_node_pageout_recursive(T, N):
 * Recursively page out the node ${N} and its children from the B+Tree ${T}.
 * The nodes must not have any locks aside from child locks.
 */
void btree_node_pageout_recursive(struct btree *, struct node *);

/**
 * btree_node_dirty(T, N):
 * The node ${N} must have type NODE_TYPE_LEAF or NODE_TYPE_PARENT and must
 * have state NODE_STATE_CLEAN.  For the node ${N} and parents as required,
 * create a new DIRTY node and mark the existing node as SHADOW.  The node
 * ${N} must be locked.  Return the new dirty node.
 */
struct node * btree_node_dirty(struct btree *, struct node *);

/**
 * btree_node_split_nparts(T, N):
 * Return the number of nodes into which the node ${N} belonging to the
 * B+Tree ${T} will be split by ntree_node_split.
 */
size_t btree_node_split_nparts(struct btree *, struct node *);

/**
 * btree_node_split(T, N, keys, parents, nparts):
 * Split the node ${N} belonging to the B+Tree ${T} into parts which are
 * small enough to be serialized to a single page.  Write the resulting
 * nodes into ${parents} and the separating keys into ${keys}; write the
 * number of parts into ${nparts} (this value must match the value returned
 * by btree_node_split_nparts).  Free the node ${N}.  On failure, return
 * with ${N} unmodified.
 */
int btree_node_split(struct btree *, struct node *,
    const struct kvldskey **, struct node **, size_t *);

/**
 * btree_node_merge(T, c_in, k_in, c_out, k_out, nsep):
 * Merge c_in[0 .. nsep] into a single node, and store it as c_out[0].
 * Separator keys, if needed, are in k_in[0 .. nsep - 1].  If the merge
 * fails, copy the (unmodified) nodes c_in[0 .. nsep] to c_out[0 .. nsep]
 * and the separator keys k_in[0 .. nsep - 1] to k_out[0 .. nsep - 1].
 */
int btree_node_merge(struct btree *, struct node **, const struct kvldskey **,
    struct node **, const struct kvldskey **, size_t);

#endif /* !BTREE_NODE_H_ */

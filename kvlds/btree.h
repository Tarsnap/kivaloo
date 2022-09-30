#ifndef BTREE_H_
#define BTREE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct cleaner;
struct node;
struct wire_requestqueue;

/* B+Tree structure. */
struct btree {
	size_t pagelen;			/* Page length (in bytes). */
	size_t poolsz;			/* Size of page pool. */
	uint64_t nextblk;		/* Next available block #. */
	struct wire_requestqueue * LBS;	/* LBS request queue. */

	/**
	 * Invariants:
	 * 1. root_shadow->state != NODE_STATE_DIRTY.
	 * 2. root_dirty->state != NODE_STATE_SHADOW.
	 * 3. (root_shadow == root_dirty) <==>
	 *    (root_shadow->state == NODE_STATE_CLEAN) <==>
	 *    (root_dirty->state == NODE_STATE_CLEAN).
	 * 4. All nodes in P are reachable via root_shadow or root_dirty.
	 */
	struct node * root_shadow;	/* Root node in shadow tree. */
	struct node * root_dirty;	/* Root node in dirty tree. */
	struct pool * P;		/* Page pool. */

	/* Used to periodically call FREE(). */
	void * gc_timer;		/* Cookie from events_timer. */

	/* Required for cleaning. */
	struct cleaner * cstate;	/* Cleaner state. */
	uint64_t nnodes;		/* Size of the dirty tree. */
	uint64_t npages;		/* # pages of storage used. */
};

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
 * This function may call events_run() internally.
 */
struct btree * btree_init(struct wire_requestqueue *, uint64_t, uint64_t,
    uint64_t *, uint64_t *, double);

/**
 * btree_balance(T, callback, cookie):
 * Rebalance the B+Tree ${T}, and invoke the provided callback.
 */
int btree_balance(struct btree *, int (*)(void *), void *);

/**
 * btree_mlen(T):
 * Fill in the matching-prefix-length values in dirty nodes in the tree ${T}.
 */
void btree_mlen(struct btree *);

/**
 * btree_sync(T, callback, cookie):
 * Serialize and write dirty nodes from the B+Tree ${T}; mark said nodes as
 * clean; free the shadow tree; and invoke the provided callback.
 */
int btree_sync(struct btree *, int (*)(void *), void *);

/**
 * btree_sanity(T):
 * Perform sanity-checks on the tree ${T}.  This is time consuming (it will
 * touch every paged-in node) and thus only exists for debugging purposes.
 * This function may not be invoked while there are priority-zero immediate
 * callbacks pending.
 */
void btree_sanity(struct btree *);

/**
 * btree_free(T):
 * Free the B-Tree ${T}, which must have root_shadow == root_dirty and must
 * have no pages locked other than the root node.
 */
void btree_free(struct btree *);

#endif /* !BTREE_H_ */

#ifndef _NODE_H_
#define _NODE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct cleaning;
struct kvhash;
struct kvldskey;
struct kvpair_const;
struct pool_elem;
struct reading;

/**
 * Data type for a B+Tree node.
 */
struct node {
	/* Page number for CLEAN/SHADOW nodes; -1 for DIRTY nodes. */
	uint64_t pagenum;

	/*
	 * Least page number of a leaf under this node, if CLEAN/SHADOW;
	 * -1 if DIRTY.
	 */
	uint64_t oldestleaf;

	/*
	 * Least page number of a leaf under this node which is not currently
	 * being handled by the cleaner (-1 if all the leaves under this node
	 * are being cleaned), if CLEAN/SHADOW; -1 if dirty.
	 */
	uint64_t oldestncleaf;

	/*
	 * Size of serialized page, in bytes, for CLEAN/SHADOW nodes;
	 * either the page size or -1 for DIRTY nodes.
	 */
	uint32_t pagesize;

	/* Node type. */
	unsigned int type : 2;
#define NODE_TYPE_PARENT	0	/* Parent node */
#define NODE_TYPE_LEAF		1	/* Leaf node */
#define NODE_TYPE_NP		2	/* Page not present */
#define NODE_TYPE_READ		3	/* Node is being fetched */

	/* Node state.  Must be CLEAN for !present nodes. */
	unsigned int state : 2;
#define NODE_STATE_CLEAN	0	/* The only copy of this node */
#define NODE_STATE_SHADOW	1	/* Old version of a modified node */
#define NODE_STATE_DIRTY	2	/* New version of a modified node */

	/* 1 if this node is a root; 0 otherwise or !present. */
	unsigned int root : 1;

	/*
	 * 1 if this node is being merged into the next node; 0 otherwise or
	 * if NODE_STATE_SHADOW.  The case merging && NODE_STATE_CLEAN occurs
	 * temporarily when a clean node is marked as being required for
	 * merging prior to the node being dirtied.
	 */
	unsigned int merging : 1;

	/* 1 if the node needs to be considered for merging; 0 otherwise. */
	unsigned int needmerge : 1;

	/* Height of this node (leaf = 0); -1 if !present. */
	int8_t height;

	/* Prefix length which all keys in this subtree have in common. */
	uint8_t mlen_t;

	/* Prefix length which all keys in this node have in common (LEAF). */
	uint8_t mlen_n;

	/**
	 * Invariants on nodes and their parents:
	 * 1. (root != 0) <==> (p_shadow == NULL) && (p_dirty == NULL).
	 * 2. (root == 0) && (state == NODE_STATE_CLEAN)
	 *        ==> (p_shadow != NULL) && (p_dirty != NULL).
	 * 3. (root == 0) && (state == NODE_STATE_SHADOW)
	 *        ==> (p_shadow != NULL) && (p_dirty == NULL).
	 * 4. (root == 0) && (state == NODE_STATE_DIRTY)
	 *        ==> (p_shadow == NULL) && (p_dirty != NULL).
	 * 5. (p_shadow != NULL) ==> (p_shadow->state != NODE_STATE_DIRTY).
	 * 6. (p_dirty != NULL) ==> (p_dirty->state != NODE_STATE_SHADOW).
	 * or less formally,
	 * 1. A node is a root iff it has no parents.
	 * 2. A clean non-root has a shadow parent and a clean parent.
	 * 3/4. A shadow/dirty non-root has only a shadow/dirty parent.
	 * 5/6. A shadow/dirty parent is not a dirty/shadow node.
	 */
	struct node * p_shadow;
	struct node * p_dirty;

	/* Node pool cookie; or NULL if NODE_TYPE_NP. */
	/**
	 * A node is locked:
	 * (a) once if root != 0,
	 * (b) once if state != NODE_STATE_CLEAN,
	 * (c) once if state == NODE_STATE_CLEAN && type == NODE_TYPE_LEAF &&
	 *     v.cstate != NULL,
	 * (d) once per present child node if type == NODE_TYPE_PARENT, and
	 * (e) once plus once per callback if reading.
	 * (f) once per priority-zero immediate event from btree_node_descend
	 *     or btree_find_(leaf|range).
	 *
	 * At the point when a non-zero priority immediate event, a network
	 * event, or a timer event is called, only (a)-(e) can apply.
	 */
	struct pool_elem * pool_cookie;

	/* Number of keys (N) for PARENT/LEAF nodes; -1 otherwise. */
	size_t nkeys;

	/**
	 * NP nodes have no data.  READ nodes have "reading".  PARENT nodes
	 * have "keys" and "children".  LEAF nodes have "pairs"; when dirty
	 * they sometimes also have "H", and when clean they sometimes also
	 * have "cstate".
	 *
	 * We pack these into two unions, "u" and "v", in order to save space
	 * in struct node and thereby save RAM.
	 */
	union {
		/* Fetching state iff NODE_TYPE_READ. */
		struct reading * reading;

		/* N keys iff NODE_TYPE_PARENT. */
		const struct kvldskey ** keys;

		/* N key-value pairs iff NODE_TYPE_LEAF. */
		struct kvpair_const * pairs;
	} u;

	union {
		/* N+1 children iff NODE_TYPE_PARENT. */
		struct node ** children;

		/*
		 * Temporary key-value hash table iff NODE_STATE_DIRTY &&
		 * NODE_TYPE_LEAF.
		 */
		struct kvhash * H;

		/*
		 * Log cleaning state iff NODE_STATE_CLEAN && NODE_TYPE_LEAF.
		 */
		struct cleaning * cstate;
	} v;

	/*
	 * Serialized page if node is CLEAN or SHADOW.  Keys and values
	 * point into here.  (If DIRTY, keys and values point into SHADOW
	 * nodes' serialized pages and/or into request structures.)
	 */
	uint8_t * pagebuf;
};

/**
 * node_alloc(pagenum, oldestleaf, pagesize):
 * Create and return a node with the specified ${pagenum}, ${oldestleaf}, and
 * ${pagesize} of type NODE_TYPE_NP.
 */
struct node * node_alloc(uint64_t, uint64_t, uint32_t);

/**
 * node_free(N):
 * Free the node ${N}, which must have type NODE_TYPE_NP.
 */
void node_free(struct node *);

/**
 * node_present(N):
 * Non-zero if ${N} is a PARENT or a LEAF.
 */
static inline int
node_present(struct node * N)
{

	return ((N->type == NODE_TYPE_PARENT) || (N->type == NODE_TYPE_LEAF));
}

/**
 * node_hasplock(N):
 * Non-zero if ${N} holds locks on its parent nodes.
 */
static inline int
node_hasplock(struct node * N)
{

	return (N->type != NODE_TYPE_NP);
}

#endif /* !_NODE_H_ */

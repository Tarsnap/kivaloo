#ifndef _BTREE_MUTATE_H_
#define _BTREE_MUTATE_H_

/* Opaque types. */
struct kvldskey;
struct node;

/**
 * btree_mutate_mutable(N):
 * Make the leaf node ${N} mutable.
 */
int btree_mutate_mutable(struct node *);

/**
 * btree_mutate_find(N, k):
 * Search for the key ${k} in the mutable leaf node ${N}.  Return the kvpair
 * in which it belongs.
 */
struct kvpair_const * btree_mutate_find(struct node *,
    const struct kvldskey *);

/**
 * btree_mutate_add(N, pos, k, v):
 * Add the key-value pair ${k}/${v} to the mutable leaf node ${N} in position
 * ${pos}, and update structures.
 */
int btree_mutate_add(struct node *, struct kvpair_const *,
    const struct kvldskey *, const struct kvldskey *);

/**
 * btree_mutate_immutable(N):
 * Mutations on the leaf node ${N} are done (for now).
 */
int btree_mutate_immutable(struct node *);

#endif /* !_BTREE_MUTATE_H_ */

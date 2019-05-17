#ifndef _BTREE_FIND_H_
#define _BTREE_FIND_H_

#include <stddef.h>

/* Opaque types. */
struct btree;
struct kvldskey;
struct kvpair_const;
struct node;

/**
 * btree_find_kvpair(N, k):
 * Search for the key ${k} in the B+Tree node ${N}.  Return a pointer to the
 * key-value pair, or NULL if the key is not present.
 */
struct kvpair_const * btree_find_kvpair(struct node *,
    const struct kvldskey *);

/**
 * btree_find_child(N, k):
 * Search for the key ${k} in the B+Tree parent node ${N}.  Return the
 * number of the child responsible for the key ${key}.
 */
size_t btree_find_child(struct node *, const struct kvldskey *);

/**
 * btree_find_leaf(T, N, k, callback, cookie):
 * Search for the key ${k} in the subtree of ${T} rooted at the node ${N}.
 * Invoke ${callback}(${cookie}, L) with the node ${L} locked, where ${L} is
 * the node under ${N} where the key ${k} should appear.
 */
int btree_find_leaf(struct btree *, struct node *, const struct kvldskey *,
    int (*)(void *, struct node *), void *);

/**
 * btree_find_range(T, N, k, h, callback, cookie):
 * Search for a node of height ${h} or less in the subtree of ${T} rooted at
 * ${N} which is responsible for a range including the key ${k}.  Invoke
 * ${callback}(${cookie}, L, e} with the node ${L} locked, where ${L} is the
 * node in question and ${e} is the endpoint of the range for which ${L} is
 * responsible (or "" if ${L} extends to the end of the keyspace).  The
 * callback is responsible for freeing e.
 */
int btree_find_range(struct btree *, struct node *, const struct kvldskey *,
    int, int (*)(void *, struct node *, struct kvldskey *), void *);

#endif /* !_BTREE_FIND_H_ */

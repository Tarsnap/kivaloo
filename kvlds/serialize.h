#ifndef _SERIALIZE_H_
#define _SERIALIZE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct btree;
struct node;

/**
 * The size of a leaf non-root node is:
 *     SERIALIZE_OVERHEAD +
 *         sum(KSS(key[i]), i = 0 .. nkeys) +
 *         sum(KSS(value[i]), i = 0 .. nkeys)
 * where KSS(x) is kvldskey_serial_size(x).
 *
 * The size of a parent non-root node is:
 *     SERIALIZE_OVERHEAD +
 *         SERIALIZE_PERCHILD * (nkeys + 1) +
 *         sum(KSS(key[i]), i = 0 .. nkeys)
 *
 * The size of a root node is SERIALIZE_ROOT bytes more than the size of an
 * identical non-root node.
 */
#define SERIALIZE_OVERHEAD	10
#define SERIALIZE_ROOT		8
#define SERIALIZE_PERCHILD	20

/**
 * serialize(T, N, buflen):
 * Serialize the dirty node ${N} into a newly allocated page buffer.  Adjust
 * key and value pointers to point into this new buffer.
 */
int serialize(struct btree *, struct node *, size_t);

/**
 * deserialize(N, buf, buflen):
 * Deserialize the node ${N} out of the ${buflen}-byte page buffer ${buf}.
 * Extra data held in the serialized root node is not processed.
 */
int deserialize(struct node *, const uint8_t *, size_t);

/**
 * deserialize_root(T, buf):
 * For a ${buf} for which deserialize(N, ${buf}, buflen) succeeded and set
 * N->root to 1, parse extra root page data into the B+tree ${T}.
 */
int deserialize_root(struct btree *, const uint8_t *);

/**
 * serialize_size(N):
 * Return the size of the page created by serializing the node ${N}.
 */
size_t serialize_size(struct node *);

/**
 * serialize_merge_size(N):
 * Return the size by which a page will increase by having the node ${N}
 * merged into it (excluding any separator key for parent nodes).
 */
size_t serialize_merge_size(struct node *);

#endif /* !_SERIALIZE_H_ */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "btree.h"
#include "kvldskey.h"
#include "kvpair.h"
#include "imalloc.h"
#include "sysendian.h"
#include "warnp.h"

#include "node.h"

#include "serialize.h"

/**
 * B+Tree page format:
 * offset length data
 * ====== ====== ====
 *      0     6   "KVLDS\0"
 *      6     2   BE number of keys (N)
 *      8     1   X = Height + 0x80 * rootedness:
 *                    0x00 - Non-root leaf node.
 *                    X    - Non-root parent node of height X.
 *                    0x80 - Root leaf node.
 *                    X    - Root parent node of height X - 0x80.
 *      9     1   Length of prefix shared by all keys under this node
 * if non-root:
 *     10   ???   DATA
 * if root:
 *     10     8   BE number of nodes
 *     18   ???   DATA
 *
 * The DATA for a leaf node is:
 *      0   ???   Serialized key #0
 *       ...
 *    ???   ???   Serialized key #(N-1)
 *    ???   ???   Serialized value #0
 *       ...
 *    ???   ???   Serialized value #(N-1)
 *
 * The DATA for a non-leaf node is:
 *      0   ???   Serialized key #0
 *       ...
 *    ???   ???   Serialized key #(N-1)
 *    ???    20   Child #0
 *       ...
 *    ???    20   Child #N
 * where a Child is
 *      0     8   BE page # of child
 *      8     8   BE page # of oldest leaf under child
 *     16     4   BE size of child page in bytes (excl zero padding)
 *
 * A serialized (key|value) is a one-byte length followed by 0--255 bytes of
 * key or value data.
 *
 * Thus the size of a leaf node is 10 + 2*N + sum(len(key)) + sum(len(value)),
 * and the size of a non-leaf node is 30 + 21*N + sum(len(key)).
 *
 * IMPORTANT: If the serialized format changes, values in serialize.h might
 * need to be updated.
 */

/**
 * serialize(T, N, buf, buflen):
 * Serialize the node ${N} from the B+tree ${T} into the ${buflen}-byte page
 * buffer ${buf}, padding with zero bytes to the end of the buffer.  The
 * caller must ensure that the serialized page will fit in the provided
 * buffer.
 */
void
serialize(struct btree * T, struct node * N, uint8_t * buf, size_t buflen)
{
	size_t pagelen;
	uint8_t * p = buf;
	size_t i;

	/* Get the page length.  This also sets N->pagelen. */
	pagelen = serialize_size(N);

	/* Sanity check: The page should fit into the buffer. */
	assert(pagelen <= buflen);

	/* Copy magic. */
	memcpy(p, "KVLDS\0", 6);
	p += 6;

	/* Write out the number of keys. */
	be16enc(p, N->nkeys);
	p += 2;

	/* Write height and rootedness. */
	if (N->root)
		*p = 0x80 + N->height;
	else
		*p = N->height;
	p += 1;

	/* Write the matching prefix length. */
	*p = N->mlen;
	p += 1;

	/* If this is a root, write the size of the tree. */
	if (N->root) {
		be64enc(p, T->nnodes);
		p += 8;
	}

	/* Write out node data. */
	if (N->type == NODE_TYPE_LEAF) {
		/* Write out the keys. */
		for (i = 0; i < N->nkeys; i++) {
			kvldskey_serialize(N->u.pairs[i].k, p);
			p += kvldskey_serial_size(N->u.pairs[i].k);
		}

		/* Write out the values. */
		for (i = 0; i < N->nkeys; i++) {
			kvldskey_serialize(N->u.pairs[i].v, p);
			p += kvldskey_serial_size(N->u.pairs[i].v);
		}
	} else {
		/* Write out the keys. */
		for (i = 0; i < N->nkeys; i++) {
			kvldskey_serialize(N->u.keys[i], p);
			p += kvldskey_serial_size(N->u.keys[i]);
		}

		/* Write out the child structures. */
		for (i = 0; i <= N->nkeys; i++) {
			/* Sanity check: Merging should be complete. */
			assert(N->v.children[i]->merging == 0);

			/* Page number of child. */
			be64enc(p, N->v.children[i]->pagenum);
			p += 8;

			/* Page number of child's oldest leaf. */
			be64enc(p, N->v.children[i]->oldestleaf);
			p += 8;

			/* Page size of leaf. */
			be32enc(p, N->v.children[i]->pagesize);
			p += 4;
		}
	}

	/* Sanity-check: Make sure we computed the size correctly. */
	assert(p == buf + pagelen);

	/* Zero the remaining space. */
	memset(p, 0, buflen - pagelen);
}

/**
 * deserialize(N, buf, buflen):
 * Deserialize the node ${N} out of the ${buflen}-byte page buffer ${buf}.
 * Extra data held in the serialized root node is not processed.
 */
int
deserialize(struct node * N, const uint8_t * buf, size_t buflen)
{
	const uint8_t * p = buf;
	size_t i, klen;

	/*
	 * Clear errno; we will use it to distinguish between internal errors
	 * (i.e., ENOMEM) and invalid page data.
	 */
	errno = 0;

	/*
	 * Sanity check: We can only deserialize a page into a node which is
	 * being fetched (NODE_TYPE_READ) and clean (NODE_STATE_CLEAN).
	 */
	assert(N->type == NODE_TYPE_READ);
	assert(N->state == NODE_STATE_CLEAN);

	/* Check magic. */
	if (buflen < 6)
		goto err1;
	if (memcmp(p, "KVLDS\0", 6))
		goto err1;
	p += 6; buflen -= 6;

	/* Parse # of keys. */
	if (buflen < 2)
		goto err1;
	N->nkeys = be16dec(p);
	p += 2; buflen -= 2;

	/* Parse height and rootedness. */
	if (buflen < 1)
		goto err1;
	if (p[0] & 0x80)
		N->root = 1;
	else
		N->root = 0;
	N->height = p[0] & 0x7f;
	if (N->height)
		N->type = NODE_TYPE_PARENT;
	else
		N->type = NODE_TYPE_LEAF;
	p += 1; buflen -= 1;

	/* Parse matching prefix length. */
	N->mlen = p[0];
	p += 1; buflen -= 1;

	/* Skip root data if appropriate. */
	if (N->root) {
		p += 8;
		buflen -= 8;
	}

	/* Parse node data. */
	if (N->type == NODE_TYPE_LEAF) {
		/* Allocate array of key-value pairs. */
		if (IMALLOC(N->u.pairs, N->nkeys, struct kvpair))
			goto err1;

		/* Initialize to NULL to simplify error path. */
		for (i = 0; i < N->nkeys; i++)
			N->u.pairs[i].k = N->u.pairs[i].v = NULL;

		/* Parse keys. */
		for (i = 0; i < N->nkeys; i++) {
			if ((klen = kvldskey_unserialize(&N->u.pairs[i].k,
			    p, buflen)) == 0)
				goto err2;
			p += klen;
			buflen -= klen;
		}

		/* Parse values. */
		for (i = 0; i < N->nkeys; i++) {
			if ((klen = kvldskey_unserialize(&N->u.pairs[i].v,
			    p, buflen)) == 0)
				goto err2;
			p += klen;
			buflen -= klen;
		}

		/* Make sure that the rest of the page is zeros. */
		while (buflen) {
			if (*p != 0)
				goto err2;
			p++; buflen--;
		}
	} else {
		/* Allocate array of keys. */
		if (IMALLOC(N->u.keys, N->nkeys, struct kvldskey *))
			goto err1;

		/* Initialize keys to NULL to simplify error path. */
		for (i = 0; i < N->nkeys; i++)
			N->u.keys[i] = NULL;

		/* Parse keys. */
		for (i = 0; i < N->nkeys; i++) {
			if ((klen = kvldskey_unserialize(&N->u.keys[i],
			    p, buflen)) == 0)
				goto err3;
			p += klen;
			buflen -= klen;
		}

		/* Allocate array of children. */
		if (IMALLOC(N->v.children, N->nkeys + 1, struct node *))
			goto err3;

		/* Initialize children to NULL to simplify error path. */
		for (i = 0; i <= N->nkeys; i++)
			N->v.children[i] = NULL;

		/* Parse children. */
		for (i = 0; i <= N->nkeys; i++) {
			/* Create child node. */
			if (buflen < SERIALIZE_PERCHILD)
				goto err4;
			if ((N->v.children[i] = node_alloc(be64dec(&p[0]),
			    be64dec(&p[8]), be32dec(&p[16]))) == NULL)
				goto err4;
			N->v.children[i]->p_shadow =
			    N->v.children[i]->p_dirty = N;
			p += SERIALIZE_PERCHILD; buflen -= SERIALIZE_PERCHILD;
		}

		/* Make sure that the rest of the page is zeros. */
		while (buflen) {
			if (*p != 0)
				goto err4;
			p++; buflen--;
		}
	}

	/* Success! */
	return (0);

	/*
	 * PARENT parsing error handling path.
	 */
err4:
	for (i = 0; i <= N->nkeys; i++)
		node_free(N->v.children[i]);
	free(N->v.children);
	N->v.children = NULL;

err3:
	for (i = 0; i < N->nkeys; i++)
		kvldskey_free(N->u.keys[i]);
	free(N->u.keys);
	N->u.keys = NULL;

	/*
	 * LEAF parsing error handling path.
	 */
err2:
	for (i = 0; i < N->nkeys; i++) {
		kvldskey_free(N->u.pairs[i].v);
		kvldskey_free(N->u.pairs[i].k);
	}
	free(N->u.pairs);
	N->u.pairs = NULL;

	/*
	 * LEAF+PARENT merged error handling path.
	 */
err1:
	if (errno != 0)
		warnp("Error parsing page");
	else
		warn0("Invalid page read");

	/* Failure! */
	return (-1);
}

/**
 * deserialize_root(T, buf):
 * For a ${buf} for which deserialize(N, ${buf}, buflen) succeeded and set
 * N->root to 1, parse extra root page data into the B+tree ${T}.
 */
int
deserialize_root(struct btree * T, const uint8_t * buf)
{

	/* The size of the tree is stored at offset SERIALIZE_OVERHEAD. */
	T->nnodes = be64dec(&buf[SERIALIZE_OVERHEAD]);

	/* Success! */
	return (0);
}

/**
 * serialize_size(N):
 * Return the size of the page created by serializing the node ${N}.
 */
size_t
serialize_size(struct node * N)
{
	size_t size;
	size_t i;

	/* If we have a stored size, return it immediately. */
	if (N->pagesize != (uint32_t)(-1))
		return (N->pagesize);

	/* "KVLDS\0". */
	size = 6;

	/* BE number of keys. */
	size += 2;

	/* Rootedness and height. */
	size += 1;

	/* Matching prefix length. */
	size += 1;

	/* Sanity check vs. values in serialize.h. */
	assert(size == SERIALIZE_OVERHEAD);

	/* Extra root data. */
	if (N->root) {
		size += 8;
		assert(size == SERIALIZE_OVERHEAD + SERIALIZE_ROOT);
	}

	/* Node data and keys. */
	if (N->type == NODE_TYPE_LEAF) {
		for (i = 0; i < N->nkeys; i++) {
			size += kvldskey_serial_size(N->u.pairs[i].k);
			size += kvldskey_serial_size(N->u.pairs[i].v);
		}
	} else {
		for (i = 0; i < N->nkeys; i++) {
			if (N->v.children[i]->merging == 0) {
				/* Child. */
				size += SERIALIZE_PERCHILD;

				/* Separator key. */
				size += kvldskey_serial_size(N->u.keys[i]);
			}
		}

		/* Last child. */
		size += SERIALIZE_PERCHILD;
	}

	/* Cache the page size. */
	N->pagesize = size;

	/* Return the computed size. */
	return (size);
}

/**
 * serialize_merge_size(N):
 * Return the size by which a page will increase by having the node ${N}
 * merged into it (excluding any separator key for parent nodes).
 */
size_t
serialize_merge_size(struct node * N)
{
	size_t headerlen;

	/*
	 * The merge size is just the serialized size minus the overhead
	 * size of the page header.
	 */
	if (N->root)
		headerlen = SERIALIZE_OVERHEAD + SERIALIZE_ROOT;
	else
		headerlen = SERIALIZE_OVERHEAD;
	return (serialize_size(N) - headerlen);
}

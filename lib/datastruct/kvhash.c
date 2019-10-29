#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crc32c.h"
#include "imalloc.h"
#include "kvldskey.h"
#include "kvpair.h"
#include "sysendian.h"

#include "kvhash.h"

/* Rehash the table into double the space. */
static int
rehash(struct kvhash * H)
{
	size_t new_nslots;
	struct kvpair_const * new_pairs;
	uint32_t * new_hashes;
	size_t i, pos;

	/* Sanity check. */
	assert((H->nslots > 0) && (H->nslots <= SIZE_MAX / 2));

	/* Double the table size. */
	new_nslots = H->nslots * 2;

	/* Allocate new arrays. */
	if (IMALLOC(new_pairs, new_nslots, struct kvpair_const))
		goto err0;
	if (IMALLOC(new_hashes, new_nslots, uint32_t))
		goto err1;

	/* Nothing in the new table yet. */
	memset(new_pairs, 0, new_nslots * sizeof(struct kvpair_const));

	/* Scan the old table and move entries. */
	for (i = 0; i < H->nslots; i++) {
		/* Skip empty slots. */
		if (H->pairs[i].k == NULL)
			continue;

		/* Look for an empty slot in the new table. */
		pos = H->hashes[i] & (new_nslots - 1);
		while (new_pairs[pos].k != NULL)
			pos = (pos + 1) & (new_nslots - 1);

		/* Copy the key, value, and hash across. */
		new_pairs[pos].k = H->pairs[i].k;
		new_pairs[pos].v = H->pairs[i].v;
		new_hashes[pos] = H->hashes[i];
	}

	/* Free the old arrays. */
	free(H->pairs);
	free(H->hashes);

	/* Attach new arrays to the hash table. */
	H->pairs = new_pairs;
	H->hashes = new_hashes;
	H->nslots = new_nslots;

	/* Success! */
	return (0);

err1:
	free(new_pairs);
err0:
	/* Failure! */
	return (-1);
}

/* Compute the hash of a key. */
static uint32_t
hash(const struct kvldskey * k)
{
	CRC32C_CTX ctx;
	uint32_t h;

	/* Compute CRC32C(k). */
	CRC32C_Init(&ctx);
	CRC32C_Update(&ctx, k->buf, k->len);
	CRC32C_Final((uint8_t *)&h, &ctx);

	/* Return hash value. */
	return (h);
}

/**
 * kvhash_init(void):
 * Return an empty kvhash.
 */
struct kvhash *
kvhash_init(void)
{
	struct kvhash * H;

	/* Allocate a kvhash. */
	if ((H = malloc(sizeof(struct kvhash))) == NULL)
		goto err0;

	/* We start with 4 slots. */
	H->nslots = 4;
	if (IMALLOC(H->pairs, H->nslots, struct kvpair_const))
		goto err1;
	if (IMALLOC(H->hashes, H->nslots, uint32_t))
		goto err2;

	/* This table is empty. */
	H->nkeys = 0;
	memset(H->pairs, 0, H->nslots * sizeof(struct kvpair_const));

	/* Success! */
	return (H);

err2:
	free(H->pairs);
err1:
	free(H);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * kvhash_search(H, k):
 * Search for the key ${k} in the kvhash ${H}.  Return a pointer to the
 * kvpair structure where the key appears or would appear if inserted.  Write
 * the hash value into the corresponding location in the hashes array.
 */
struct kvpair_const *
kvhash_search(struct kvhash * H, const struct kvldskey * k)
{
	size_t pos;
	uint32_t h;

	/* Compute the hash. */
	h = hash(k);

	/* Scan the table until we find an empty slot or the key. */
	pos = h & (H->nslots - 1);
	do {
		/* If this slot is empty, stop. */
		if (H->pairs[pos].k == NULL)
			break;

		/* If the hash and key match, stop. */
		if ((H->hashes[pos] == h) &&
		    (kvldskey_cmp(k, H->pairs[pos].k) == 0))
			break;

		/* Move on to the next slot. */
		pos = (pos + 1) & (H->nslots - 1);
	} while (1);

	/*
	 * Write the hash here.  If the key is already present, this has no
	 * effect; if the key is not present, this ensures that they hash is
	 * in place if/when the key is added.
	 */
	H->hashes[pos] = h;

	/* Return a pointer to the kvpair struct. */
	return (&H->pairs[pos]);
}

/**
 * kvhash_postadd(H):
 * Record that key-value pair has been added to the kvhash ${H}.  Rehash
 * (expand) the table if necessary.
 */
int
kvhash_postadd(struct kvhash * H)
{

	/* We've added an entry to the hash table. */
	H->nkeys += 1;

	/* Rehash if the table is more than 3/4 full. */
	if (H->nkeys + H->nslots / 4 > H->nslots) {
		if (rehash(H))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * kvhash_free(H):
 * Free the kvhash ${H}.  Do not free the keys or values it holds.
 */
void
kvhash_free(struct kvhash * H)
{

	/* Be compatible with free(NULL). */
	if (H == NULL)
		return;

	/* Free the hash table. */
	free(H->hashes);
	free(H->pairs);
	free(H);
}

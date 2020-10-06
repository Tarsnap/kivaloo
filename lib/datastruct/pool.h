#ifndef _POOL_H_
#define _POOL_H_

#include <stddef.h>

/* Opaque pool structure. */
struct pool;

/* Opaque pool element structure. */
struct pool_elem;

/**
 * pool_init(nrec, offset):
 * Create a pool with target size ${nrec} records, where each record has a
 * (struct pool_elem *) reserved at offset ${offset}.
 */
struct pool * pool_init(size_t, size_t);

/**
 * pool_rec_add(P, rec, evict):
 * Add the record ${rec} to the pool ${P} with lock count 1.  If a record
 * must be evicted from the pool, return it via ${evict}.  If no records have
 * lock count 0 (i.e., no records can be evicted) and the pool is already at
 * the target size, ${rec} will still be added to the pool, and the pool will
 * only return to its target size via calls to pool_rec_free.
 */
int pool_rec_add(struct pool *, void *, void **);

/**
 * pool_rec_free(P, rec):
 * Remove the record ${rec} from the pool ${P}.  The record ${rec} must have
 * lock count 1.
 */
void pool_rec_free(struct pool *, void *);

/**
 * pool_rec_lock(P, rec):
 * Increment the lock count of the record ${rec} in the pool ${P}.  A record
 * with non-zero lock count cannot be evicted from the pool.
 */
static void pool_rec_lock(struct pool *, void *);

/**
 * pool_rec_unlock(P, rec):
 * Decrement the lock count of the record ${rec} in the pool ${P}.
 */
static void pool_rec_unlock(struct pool *, void *);

/**
 * pool_rec_lockcount(P, rec):
 * Return the lock count of the record ${rec} in the pool ${P}.
 */
size_t pool_rec_lockcount(struct pool *, void *);

/**
 * pool_free(P):
 * Free the pool ${P}, which must be empty.
 */
void pool_free(struct pool *);

/**
 * Declarations after this point are internal and should not be used directly.
 */

#ifndef assert
#include <assert.h>
#endif
#include <stdint.h>

/* Pool structure. */
struct pool {
	size_t size;		/* Target size of pool. */
	size_t used;		/* Current size of pool. */
	void * evict_head;	/* First record to evict. */
	void * evict_tail;	/* Last record to evict. */
	size_t offset;		/* Offset of rec.(struct pool_elem). */
};

/* Pool element structure. */
struct pool_elem {
	/*
	 * Number of times _unlock must be called before this record can be
	 * evicted from the pool.
	 */
	size_t wire_count;

	/* If wire_count == 0, next element to be evicted. */
	void * next;

	/* If wire_count == 0, previous element to be evicted. */
	void * prev;
};

/* Find the pool_elem within a record. */
#define get_pool_elem(P, rec)					\
	(*(struct pool_elem **)(void *)((uint8_t *)(rec) + (P->offset)))

/**
 * pool_addqueue(P, rec):
 * Add the record ${rec} to the eviction queue for the pool ${P}.
 */
void pool_addqueue(struct pool *, void *);

/**
 * pool_delqueue(P, rec):
 * Delete the record ${rec} from the eviction queue for the pool ${P}.
 */
void pool_delqueue(struct pool *, void *);

/**
 * pool_rec_lock(P, rec):
 * Increment the lock count of the record ${rec} in the pool ${P}.  A record
 * with non-zero lock count cannot be evicted from the pool.
 */
static inline void
pool_rec_lock(struct pool * P, void * rec)
{

	/* Increment the wire count. */
	get_pool_elem(P, rec)->wire_count += 1;

	/* Remove from the evictable queue if necessary. */
	if (get_pool_elem(P, rec)->wire_count == 1)
		pool_delqueue(P, rec);
}

/**
 * pool_rec_unlock(P, rec):
 * Decrement the lock count of the record ${rec} in the pool ${P}.
 */
static inline void
pool_rec_unlock(struct pool * P, void * rec)
{

	/* Make sure the lock count isn't already zero. */
	assert(get_pool_elem(P, rec)->wire_count > 0);

	/* Decrement the wire count. */
	get_pool_elem(P, rec)->wire_count -= 1;

	/* Add to the evictable queue if necessary. */
	if (get_pool_elem(P, rec)->wire_count == 0)
		pool_addqueue(P, rec);
}

#endif /* !_POOL_H_ */

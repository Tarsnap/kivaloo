#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "pool.h"

/**
 * pool_init(nrec, offset):
 * Create a pool with target size ${nrec} records, where each record has a
 * (struct pool_elem *) reserved at offset ${offset}.
 */
struct pool *
pool_init(size_t nrec, size_t offset)
{
	struct pool * P;

	/* Allocate a pool structure. */
	if ((P = malloc(sizeof(struct pool))) == NULL)
		goto err0;

	/* Initialize. */
	P->size = nrec;
	P->used = 0;
	P->evict_head = P->evict_tail = NULL;
	P->offset = offset;

	/* Success! */
	return (P);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * pool_rec_add(P, rec, evict):
 * Add the record ${rec} to the pool ${P} with lock count 1.  If a record
 * must be evicted from the pool, return it via ${evict}.  If no records have
 * lock count 0 (i.e., no records can be evicted) and the pool is already at
 * the target size, ${rec} will still be added to the pool, and the pool will
 * only return to its target size via calls to pool_rec_free.
 */
int
pool_rec_add(struct pool * P, void * rec, void ** evict)
{

	/* Create a pool_elem structure for this record. */
	if ((get_pool_elem(P, rec) =
	    malloc(sizeof(struct pool_elem))) == NULL)
		goto err0;
	get_pool_elem(P, rec)->wire_count = 1;

	/* Add the record to the pool. */
	P->used += 1;

	/* Evict a record if necessary and possible. */
	if ((P->used > P->size) && (P->evict_head != NULL)) {
		/* Grab the record at the head of the evict queue. */
		*evict = P->evict_head;

		/* Remove said record from the queue. */
		pool_delqueue(P, *evict);

		/* Remove the record from the pool. */
		free(get_pool_elem(P, *evict));
		P->used -= 1;
	} else {
		*evict = NULL;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * pool_rec_free(P, rec):
 * Remove the record ${rec} from the pool ${P}.  The record ${rec} must have
 * lock count 1.
 */
void
pool_rec_free(struct pool * P, void * rec)
{

	/* Make sure that the lock count is 1. */
	assert(get_pool_elem(P, rec)->wire_count == 1);

	/* Remove the record from the pool. */
	free(get_pool_elem(P, rec));
	P->used -= 1;
}

/**
 * pool_rec_lockcount(P, rec):
 * Return the lock count of the record ${rec} in the pool ${P}.
 */
size_t
pool_rec_lockcount(struct pool * P, void * rec)
{

	/* Just read it straight out of the record. */
	return (get_pool_elem(P, rec)->wire_count);
}

/**
 * pool_free(P):
 * Free the pool ${P}, which must be empty.
 */
void
pool_free(struct pool * P)
{

	/* Be compatible with free(NULL). */
	if (P == NULL)
		return;

	/* Make sure the pool is empty. */
	assert(P->used == 0);

	/* Free the pool. */
	free(P);
}

/**
 * pool_addqueue(P, rec):
 * Add the record ${rec} to the eviction queue for the pool ${P}.
 */
void
pool_addqueue(struct pool * P, void * rec)
{

	/* This record has no successor. */
	get_pool_elem(P, rec)->next = NULL;

	/* This record's predecessor is the current last record (if any). */
	get_pool_elem(P, rec)->prev = P->evict_tail;

	/* Add this record to the queue. */
	if (P->evict_head == NULL)
		P->evict_head = rec;
	else
		get_pool_elem(P, P->evict_tail)->next = rec;

	/* This record is now the last record in the queue. */
	P->evict_tail = rec;
}

/**
 * pool_delqueue(P, rec):
 * Delete the record ${rec} from the eviction queue for the pool ${P}.
 */
void
pool_delqueue(struct pool * P, void * rec)
{
	void * next = get_pool_elem(P, rec)->next;
	void * prev = get_pool_elem(P, rec)->prev;

	/* If this is the only record in the queue, it becomes empty. */
	if ((P->evict_head == rec) && (P->evict_tail == rec)) {
		P->evict_head = P->evict_tail = NULL;
	} else
	/* If this is the head, we have a new head. */
	    if (P->evict_head == rec) {
		P->evict_head = next;
		get_pool_elem(P, next)->prev = NULL;
	} else
	/* If this is the tail, we have a new tail. */
	    if (P->evict_tail == rec) {
		P->evict_tail = prev;
		get_pool_elem(P, prev)->next = NULL;
	} else
	/* This is in the middle; point prev and next to each other. */
	    {
		get_pool_elem(P, next)->prev = prev;
		get_pool_elem(P, prev)->next = next;
	}
}

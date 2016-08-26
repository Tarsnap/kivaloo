#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "elasticqueue.h"

#include "seqptrmap.h"

struct seqptrmap {
	struct elasticqueue * ptrs;
	int64_t offset;
	size_t len;
};

/**
 * seqptrmap_init():
 * Return an empty sequential pointer map.  Return NULL on error.
 */
struct seqptrmap *
seqptrmap_init(void)
{
	struct seqptrmap * M;

	/* Allocate structure. */
	if ((M = malloc(sizeof(struct seqptrmap))) == NULL)
		goto err0;

	/* Nothing in here yet. */
	M->offset = 0;
	M->len = 0;

	/* Allocate empty queue. */
	if ((M->ptrs = elasticqueue_init(sizeof(void *))) == NULL)
		goto err1;

	/* Success! */
	return (M);

err1:
	free(M);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * seqptrmap_add(M, ptr):
 * Add the pointer ${ptr} to the map ${M}.  Return the associated integer;
 * or -1 on error.
 */
int64_t
seqptrmap_add(struct seqptrmap * M, void * ptr)
{

	/* Add the pointer to the end of the queue. */
	if (elasticqueue_add(M->ptrs, &ptr))
		goto err0;
	M->len += 1;

	/* Check for overflow. */
	assert(M->len <= (uint64_t)INT64_MAX);
	assert(INT64_MAX - (int64_t)M->len >= M->offset);

	/* Return associated integer. */
	return (M->offset + (int64_t)M->len - 1);

err0:
	/* Failure! */
	return (-1);
}

/**
 * seqptrmap_get(M, i):
 * Return the pointer associated with the integer ${i} in the map ${M}.  If
 * there is no associated pointer (because no pointer has been added for the
 * specified value yet, or because the associated pointer has been deleted),
 * then return NULL.  This function cannot fail.
 */
void *
seqptrmap_get(struct seqptrmap * M, int64_t i)
{

	/* No valid pointer is less than the offset. */
	if (i < M->offset)
		return (NULL);

	/*
	 * If the provided integer is not within the bounds of our elastic
	 * queue, there is no associated pointer and we return NULL.
	 */
	if ((uint64_t)(i - M->offset) >= (uint64_t)(M->len))
		return (NULL);

	/*
	 * Since the provided integer is within the bounds of our elastic
	 * queue, look up the pointer.
	 */
	return (*(void **)elasticqueue_get(M->ptrs, (size_t)(i - M->offset)));
}

/**
 * seqptrmap_getmin(M):
 * Return the minimum integer associated with a pointer in the map ${M}, or
 * -1 if the map is empty.  This function cannot fail.
 */
int64_t
seqptrmap_getmin(struct seqptrmap * M)
{

	/* If the underlying elastic queue is empty, return -1. */
	if (elasticqueue_getlen(M->ptrs) == 0)
		return (-1);

	/*
	 * Otherwise, the first pointer must be in position 0 of the queue,
	 * i.e., be associated with the integer M->offset.
	 */
	return (M->offset);
}

/**
 * seqptrmap_delete(M, i):
 * Delete the pointer associated with the integer ${i} in the map ${M}.
 */
void
seqptrmap_delete(struct seqptrmap * M, int64_t i)
{

	/* No valid pointer is less than the offset. */
	if (i < M->offset)
		return;

	/*
	 * If the provided integer is not within the bounds of our elastic
	 * queue, return without taking any action.
	 */
	if ((uint64_t)(i - M->offset) >= (uint64_t)(M->len))
		return;

	/* Delete the specified pointer by setting it to NULL. */
	*(void **)elasticqueue_get(M->ptrs, (size_t)(i - M->offset)) = NULL;

	/* Delete leading NULLs from the queue. */
	while ((elasticqueue_getlen(M->ptrs) > 0) &&
	    (*(void **)elasticqueue_get(M->ptrs, 0) == NULL)) {
		elasticqueue_delete(M->ptrs);
		M->offset += 1;
		M->len -= 1;
	}
}

/**
 * seqptrmap_free(M):
 * Free the sequential pointer map ${M}.
 */
void
seqptrmap_free(struct seqptrmap * M)
{

	/* Be compatible with free(NULL). */
	if (M == NULL)
		return;

	elasticqueue_free(M->ptrs);
	free(M);
}

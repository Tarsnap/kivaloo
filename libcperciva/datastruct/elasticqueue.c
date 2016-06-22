#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"

#include "elasticqueue.h"

struct elasticqueue {
	struct elasticarray * EA;
	size_t offset;
	size_t len;
	size_t reclen;
};

/**
 * elasticqueue_init(reclen):
 * Create and return an empty elastic queue of ${reclen}-byte records.
 */
struct elasticqueue *
elasticqueue_init(size_t reclen)
{
	struct elasticqueue * EQ;

	/* Allocate structure. */
	if ((EQ = malloc(sizeof(struct elasticqueue))) == NULL)
		goto err0;
	EQ->reclen = reclen;

	/* Create (empty) elastic array. */
	if ((EQ->EA = elasticarray_init(0, EQ->reclen)) == NULL)
		goto err1;

	/* The queue is empty so far. */
	EQ->offset = EQ->len = 0;

	/* Success! */
	return (EQ);

err1:
	free(EQ);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * elasticqueue_add(EQ, rec):
 * Add ${rec} to the end of the elastic queue ${EQ}.
 */
int
elasticqueue_add(struct elasticqueue * EQ, const void * rec)
{

	/* Add the record to the end of the elastic array. */
	if (elasticarray_append(EQ->EA, rec, 1, EQ->reclen))
		goto err0;

	/* The queue just gained a record. */
	EQ->len += 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * elasticqueue_delete(EQ):
 * Delete the record at the front of the elastic queue ${EQ}.  If the queue
 * is empty, this function will have no effect.
 *
 * As an exception to the normal rule, an elastic queue may use more memory
 * than the standard bound immediately following an elasticqueue_delete call;
 * but only if realloc(3) failed to shrink a memory allocation.
 */
void
elasticqueue_delete(struct elasticqueue * EQ)
{
	size_t i;
	void * oldpos;
	void * newpos;

	/* Return immediately if the queue is empty. */
	if (EQ->len == 0)
		return;

	/* Adjust pointers to remove the record from the logical queue. */
	EQ->offset += 1;
	EQ->len -= 1;

	/* Should we move everything to the front of the array? */
	if (EQ->offset > EQ->len) {
		/* Move records. */
		for (i = 0; i < EQ->len; i++) {
			newpos = elasticarray_get(EQ->EA, i, EQ->reclen);
			oldpos = elasticarray_get(EQ->EA, i + EQ->offset,
			    EQ->reclen);
			memcpy(newpos, oldpos, EQ->reclen);
		}

		/* Remove the unused space at the end. */
		elasticarray_shrink(EQ->EA, EQ->offset, EQ->reclen);

		/* And remember that everything is now at the front. */
		EQ->offset = 0;
	}
}

/**
 * elasticqueue_getlen(EQ):
 * Return the length of the elastic queue ${EQ}.
 */
size_t
elasticqueue_getlen(struct elasticqueue * EQ)
{

	return (EQ->len);
}

/**
 * elasticqueue_get(EQ, pos):
 * Return a pointer to the element in position ${pos} of the elastic queue
 * ${EQ}, where 0 is the head of the queue and elasticqueue_getlen(${EQ}) - 1
 * is the tail.  For values of ${pos} beyond the end of the queue, NULL will
 * be returned.
 */
void *
elasticqueue_get(struct elasticqueue * EQ, size_t pos)
{

	/* Return NULL for out-of-bounds requests. */
	if (pos >= EQ->len)
		return (NULL);

	/* Return a pointer to the record. */
	return (elasticarray_get(EQ->EA, pos + EQ->offset, EQ->reclen));
}

/**
 * elasticqueue_free(EQ):
 * Free the elastic queue ${EQ}.
 */
void
elasticqueue_free(struct elasticqueue * EQ)
{

	/* Be compatible with free(NULL). */
	if (EQ == NULL)
		return;

	/* Free the underlying elastic array. */
	elasticarray_free(EQ->EA);

	/* Free the elastic queue structure. */
	free(EQ);
}

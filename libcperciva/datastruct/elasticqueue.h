#ifndef ELASTICQUEUE_H_
#define ELASTICQUEUE_H_

#include <stddef.h>

/**
 * Elastic queues are dynamically resizing queues which remain within a
 * factor of 8 of the optimal memory consumption for the data they contain
 * and have (within a constant factor) amortized optimal running time.
 * Unlike simple linked lists, elastic queues provide random access.
 * Functions return NULL or (int)(-1) on error and set errno; other return
 * types indicate that failure is not possible.  On error, the queue will be
 * unmodified.
 */

/* Opaque elastic queue type. */
struct elasticqueue;

/**
 * elasticqueue_init(reclen):
 * Create and return an empty elastic queue of ${reclen}-byte records.
 * The value ${reclen} must be positive.
 */
struct elasticqueue * elasticqueue_init(size_t);

/**
 * elasticqueue_add(EQ, rec):
 * Add ${rec} to the end of the elastic queue ${EQ}.
 */
int elasticqueue_add(struct elasticqueue *, const void *);

/**
 * elasticqueue_delete(EQ):
 * Delete the record at the front of the elastic queue ${EQ}.  If the queue
 * is empty, this function will have no effect.
 *
 * As an exception to the normal rule, an elastic queue may use more memory
 * than the standard bound immediately following an elasticqueue_delete() call;
 * but only if realloc(3) failed to shrink a memory allocation.
 */
void elasticqueue_delete(struct elasticqueue *);

/**
 * elasticqueue_getlen(EQ):
 * Return the length of the elastic queue ${EQ}.
 */
size_t elasticqueue_getlen(struct elasticqueue *);

/**
 * elasticqueue_get(EQ, pos):
 * Return a pointer to the element in position ${pos} of the elastic queue
 * ${EQ}, where 0 is the head of the queue and elasticqueue_getlen(${EQ}) - 1
 * is the tail.  For values of ${pos} beyond the end of the queue, NULL will
 * be returned.
 */
void * elasticqueue_get(struct elasticqueue *, size_t);

/**
 * elasticqueue_free(EQ):
 * Free the elastic queue ${EQ}.
 */
void elasticqueue_free(struct elasticqueue *);

#endif /* !ELASTICQUEUE_H_ */

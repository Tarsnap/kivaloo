#ifndef _SEQPTRMAP_H_
#define _SEQPTRMAP_H_

#include <stdint.h>

/**
 * Sequential pointer map structure.  Pointers are inserted and numbered
 * starting from 0; given a pointer number, the associated pointer can be
 * fetched or deleted.  All operations take amortized O(1) time, and the
 * memory usage is at most 8 * sizeof(void *) * (max - min + 1) + O(1) where
 * max and min are the highest and lowest numbers of non-deleted pointers,
 * except if memory (re)allocation fails in seqptrmap_delete.
 */

/* Opaque sequential pointer map type. */
struct seqptrmap;

/**
 * seqptrmap_init():
 * Return an empty sequential pointer map.  Return NULL on error.
 */
struct seqptrmap * seqptrmap_init(void);

/**
 * seqptrmap_add(M, ptr):
 * Add the pointer ${ptr} to the map ${M}.  Return the associated integer.
 * On error, the map will be unmodified and -1 will be returned.
 */
int64_t seqptrmap_add(struct seqptrmap *, void *);

/**
 * seqptrmap_get(M, i):
 * Return the pointer associated with the integer ${i} in the map ${M}.  If
 * there is no associated pointer (because no pointer has been added for the
 * specified value yet, or because the associated pointer has been deleted),
 * then return NULL.  This function cannot fail.
 */
void * seqptrmap_get(struct seqptrmap *, int64_t);

/**
 * seqptrmap_getmin(M):
 * Return the minimum integer associated with a pointer in the map ${M}, or
 * -1 if the map is empty.  This function cannot fail.
 */
int64_t seqptrmap_getmin(struct seqptrmap *);

/**
 * seqptrmap_delete(M, i):
 * Delete the pointer associated with the integer ${i} in the map ${M}.
 */
void seqptrmap_delete(struct seqptrmap *, int64_t);

/**
 * seqptrmap_free(M):
 * Free the sequential pointer map ${M}.
 */
void seqptrmap_free(struct seqptrmap *);

#endif /* !_SEQPTRMAP_H_ */

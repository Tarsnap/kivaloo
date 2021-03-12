#ifndef _DOUBLEHEAP_H_
#define _DOUBLEHEAP_H_

#include <stddef.h>

/**
 * Min-heap of doubles.  Supported operations are create, add, getmin, and
 * deletemin.  Functions return NULL or (int)(-1) on error and set errno;
 * other return types indicate that failure is not possible.  On error, the
 * heap will be unmodified.
 */

/* Opaque double-heap type. */
struct doubleheap;

/**
 * doubleheap_init(void):
 * Create and return an empty heap.
 */
struct doubleheap * doubleheap_init(void);

/**
 * doubleheap_create(buf, N):
 * Create and return a heap, as in doubleheap_init, but with the ${N} doubles
 * in ${buf} as heap elements.  This is faster than creating an empty heap
 * and adding the elements individually.
 */
struct doubleheap * doubleheap_create(double *, size_t);

/**
 * doubleheap_add(H, x):
 * Add the value ${x} to the heap ${H}.
 */
int doubleheap_add(struct doubleheap *, double);

/**
 * doubleheap_getmin(H, x):
 * Set *${x} to the minimum value in the heap ${H} and return 0.  If the heap
 * is empty, return 1.
 */
int doubleheap_getmin(struct doubleheap *, double *);

/**
 * doubleheap_setmin(H, x):
 * Replace the minimum element in the heap ${H} with ${x}; this is equivalent
 * to deletemin + add, but is guaranteed to succeed.
 */
void doubleheap_setmin(struct doubleheap *, double);

/**
 * doubleheap_deletemin(H):
 * Delete the minimum element in the heap ${H}.
 */
void doubleheap_deletemin(struct doubleheap *);

/**
 * doubleheap_free(H):
 * Free the heap ${H}.
 */
void doubleheap_free(struct doubleheap *);

#endif /* !_DOUBLEHEAP_H_ */

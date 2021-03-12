#include <assert.h>
#include <stdlib.h>

#include "elasticarray.h"

#include "doubleheap.h"

ELASTICARRAY_DECL(DOUBLELIST, doublelist, double);

struct doubleheap {
	DOUBLELIST elems;
	size_t nelems;
};

/**
 * swap(elems, i, j):
 * Swap elements ${i} and ${j} in ${elems}.
 */
static void
swap(DOUBLELIST elems, size_t i, size_t j)
{
	double tmp;

	/* Swap the elements. */
	tmp = *doublelist_get(elems, i);
	*doublelist_get(elems, i) = *doublelist_get(elems, j);
	*doublelist_get(elems, j) = tmp;
}

/**
 * heapifyup(elems, i):
 * Sift up element ${i} of the elements ${elems}.
 */
static void
heapifyup(DOUBLELIST elems, size_t i)
{

	/* Iterate up the tree. */
	do {
		/* If we're at the root, we have nothing to do. */
		if (i == 0)
			break;

		/* If this is >= its parent, we're done. */
		if (*doublelist_get(elems, i) >=
		    *doublelist_get(elems, (i - 1) / 2))
			break;

		/* Swap with the parent. */
		swap(elems, i, (i - 1) / 2);

		/* Move up the tree. */
		i = (i - 1) / 2;
	} while (1);
}

/**
 * heapify(elems, i, N):
 * Sift down element number ${i} out of ${N} of the elements ${elems}.
 */
static void
heapify(DOUBLELIST elems, size_t i, size_t N)
{
	size_t min;

	/* Iterate down the tree. */
	do {
		/* Look for the minimum element out of {i, 2i+1, 2i+2}. */
		min = i;

		/* Is this bigger than element 2i+1? */
		if (2 * i + 1 < N &&
		    *doublelist_get(elems, min) >
		    *doublelist_get(elems, 2 * i + 1))
			min = 2 * i + 1;

		/* Is this bigger than element 2i+2? */
		if (2 * i + 2 < N &&
		    *doublelist_get(elems, min) >
		    *doublelist_get(elems, 2 * i + 2))
			min = 2 * i + 2;

		/* If the minimum is i, we have heap-property. */
		if (min == i)
			break;

		/* Move the minimum into position i. */
		swap(elems, min, i);

		/* Move down the tree. */
		i = min;
	} while (1);
}

/**
 * doubleheap_init(void):
 * Create and return an empty heap.
 */
struct doubleheap *
doubleheap_init(void)
{

	/* Let doubleheap_create handle this. */
	return (doubleheap_create(NULL, 0));
}

/**
 * doubleheap_create(buf, N):
 * Create and return a heap, as in doubleheap_init, but with the ${N} doubles
 * in ${buf} as heap elements.  This is faster than creating an empty heap
 * and adding the elements individually.
 */
struct doubleheap *
doubleheap_create(double * buf, size_t N)
{
	struct doubleheap * H;
	size_t i;

	/* Allocate structure. */
	if ((H = malloc(sizeof(struct doubleheap))) == NULL)
		goto err0;

	/* We will have N elements. */
	H->nelems = N;

	/* Allocate space for N heap elements. */
	if ((H->elems = doublelist_init(N)) == NULL)
		goto err1;

	/* Copy the heap elements in. */
	for (i = 0; i < N; i++)
		*doublelist_get(H->elems, i) = buf[i];

	/* Turn this into a heap. */
	for (i = N - 1; i < N; i--)
		heapify(H->elems, i, N);

	/* Success! */
	return (H);

err1:
	free(H);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * doubleheap_add(H, x):
 * Add the value ${x} to the heap ${H}.
 */
int
doubleheap_add(struct doubleheap * H, double x)
{

	/* Add the element to the end of the heap. */
	if (doublelist_append(H->elems, &x, 1))
		goto err0;
	H->nelems += 1;

	/* Move the new element up in the tree if necessary. */
	heapifyup(H->elems, H->nelems - 1);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * doubleheap_getmin(H, x):
 * Set *${x} to the minimum value in the heap ${H} and return 0.  If the heap
 * is empty, return 1.
 */
int
doubleheap_getmin(struct doubleheap * H, double * x)
{

	/* If we have any elements, the minimum is in position 0. */
	if (H->nelems) {
		*x = *doublelist_get(H->elems, 0);
		return (0);
	} else
		return (1);
}

/**
 * doubleheap_setmin(H, x):
 * Replace the minimum element in the heap ${H} with ${x}.
 */
void
doubleheap_setmin(struct doubleheap * H, double x)
{

	/* Sanity-check; We must have a minimum element. */
	assert(H->nelems > 0);

	/* Set the new value. */
	*doublelist_get(H->elems, 0) = x;

	/* Move it down the heap if necessary. */
	heapify(H->elems, 0, H->nelems);
}

/**
 * doubleheap_deletemin(H):
 * Delete the minimum element in the heap ${H}.
 */
void
doubleheap_deletemin(struct doubleheap * H)
{

	/* Sanity-check: Can't delete something which doesn't exist. */
	assert(H->nelems > 0);

	/* Move the last element to the top. */
	*doublelist_get(H->elems, 0) =
	    *doublelist_get(H->elems, H->nelems - 1);

	/* Shrink the heap. */
	doublelist_shrink(H->elems, 1);
	H->nelems--;

	/* Move it down the heap until it finds the right place. */
	heapify(H->elems, 0, H->nelems);
}

/**
 * doubleheap_free(H):
 * Free the heap ${H}.
 */
void
doubleheap_free(struct doubleheap * H)
{

	/* Behave consistently with free(NULL). */
	if (H == NULL)
		return;

	doublelist_free(H->elems);
	free(H);
}

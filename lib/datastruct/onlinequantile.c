#include <math.h>
#include <stddef.h>
#include <string.h>

#include "doubleheap.h"
#include "hazenquantile.h"
#include "imalloc.h"

#include "onlinequantile.h"

/* Quantile-computation type. */
struct onlinequantile {
	/* Elements > the current quantile point. */
	struct doubleheap * larger;

	/* Elements <= the current quantile point, negated. */
	struct doubleheap * smaller;

	/* Values on either side of the quantile. */
	double larger_min;
	double smaller_max;

	/* Parameters. */
	size_t N;
	size_t N_smaller;
	double q;
};

/**
 * onlinequantile_init(q):
 * For 0 <= ${q} <= 1, prepare to compute (online) quantiles of doubles.
 */
struct onlinequantile *
onlinequantile_init(double q)
{
	struct onlinequantile * Q;

	/* Allocate structure and initialize parameters. */
	if ((Q = malloc(sizeof(struct onlinequantile))) == NULL)
		goto err0;
	Q->larger_min = (double)INFINITY;
	Q->smaller_max = - (double)INFINITY;
	Q->N = 0;
	Q->N_smaller = 0;
	Q->q = q;

	/* Create "smaller" heap. */
	if ((Q->smaller = doubleheap_init()) == NULL)
		goto err1;

	/* Create "larger" heap. */
	if ((Q->larger = doubleheap_init()) == NULL)
		goto err2;

	/* Success! */
	return (Q);

err2:
	doubleheap_free(Q->smaller);
err1:
	free(Q);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * onlinequantile_get(Q, x):
 * Set *${x} to the current quantile value from the structure ${Q} and
 * return 0.  If there is no data, return 1.
 */
int
onlinequantile_get(struct onlinequantile * Q, double * x)
{
	size_t i;
	double r;

	/* If empty, return 1. */
	if (Q->N == 0)
		return (1);

	/* Figure out the necessary averaging. */
	hazenquantile(Q->N, Q->q, &i, &r);

	/* Sanity-check. */
	assert(Q->N_smaller - 1 == i);

	/* Are we averaging two values? */
	if (r != 0.0) {
		/* Sanity-check. */
		assert(Q->N_smaller < Q->N);

		/* Compute the average. */
		*x = Q->smaller_max + (Q->larger_min - Q->smaller_max) * r;
	} else {
		/* The median is just a single value. */
		*x = Q->smaller_max;
	}

	/* Success! */
	return (0);
}

/**
 * onlinequantile_add(Q, x):
 * Add the value ${x} to the quantile structure ${Q}.
 */
int
onlinequantile_add(struct onlinequantile * Q, double x)
{
	size_t i;
	double r;

	/* Figure out where the quantile falls after expansion. */
	hazenquantile(Q->N + 1, Q->q, &i, &r);

	/* Which heap needs to grow? */
	if (i + 1 > Q->N_smaller) {
		/* We need to expand the "smaller than quantile" heap. */

		/* Sanity-check. */
		assert(Q->N_smaller == i);

		if (x <= Q->larger_min) {
			/* Add to "smaller" heap. */
			if (doubleheap_add(Q->smaller, -x))
				goto err0;
			Q->N_smaller += 1;

			/* Update smaller_max if necessary. */
			if (Q->smaller_max < x)
				Q->smaller_max = x;
		} else {
			/*
			 * This value is going into the "larger" heap, but we
			 * want the "smaller" heap to be growing; so we need
			 * to take the least element from "larger" and move it
			 * into "smaller".
			 */

			/* Sanity-check. */
			assert(Q->N_smaller < Q->N);

			/* Add element to "smaller" heap. */
			if (doubleheap_add(Q->smaller, -Q->larger_min))
				goto err0;
			Q->N_smaller += 1;
			Q->smaller_max = Q->larger_min;

			/*
			 * Now replace the value we removed from the "larger"
			 * heap with our new value; and figure out what the
			 * new minimum value in that heap is.
			 */
			doubleheap_setmin(Q->larger, x);
			doubleheap_getmin(Q->larger, &Q->larger_min);
		}
	} else {
		/* We need to expand the "larger than quantile" heap. */

		/* Sanity-check. */
		assert(Q->N_smaller - 1 == i);

		/* Where does the new value land? */
		if (x >= Q->smaller_max) {
			/* Add to "larger" heap. */
			if (doubleheap_add(Q->larger, x))
				goto err0;

			/* Update larger_min if necessary. */
			if (Q->larger_min > x)
				Q->larger_min = x;
		} else {
			/*
			 * This value is going into the "smaller" heap, but we
			 * want the "larger" heap to be growing; so we need
			 * to take the greatest element from "smaller" and move
			 * it into "larger".
			 */

			/* Sanity-check. */
			assert(Q->N_smaller > 0);

			/* Add element to "larger" heap. */
			if (doubleheap_add(Q->larger, Q->smaller_max))
				goto err0;
			Q->larger_min = Q->smaller_max;

			/*
			 * Now replace the value we removed from the "smaller"
			 * heap with our new value; and figure out what the
			 * new maximum value in that heap is.
			 */
			doubleheap_setmin(Q->smaller, -x);
			doubleheap_getmin(Q->smaller, &Q->smaller_max);
			Q->smaller_max = - Q->smaller_max;
		}
	}

	/* We have added a new data point (somewhere). */
	Q->N += 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * onlinequantile_free(Q):
 * Free the quantile structure ${Q}.
 */
void
onlinequantile_free(struct onlinequantile * Q)
{

	/* Behave consistently with free(NULL). */
	if (Q == NULL)
		return;

	/* Free heaps. */
	doubleheap_free(Q->larger);
	doubleheap_free(Q->smaller);

	/* Free quantile structure. */
	free(Q);
}

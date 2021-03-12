#ifndef _HAZENQUANTILE_H_
#define _HAZENQUANTILE_H_

#include <assert.h>
#include <stddef.h>

/**
 * Hazen quantiles are a way of defining quantiles of a finite set of elements
 * -- e.g., the 25th percentile out of a set of 10 elements.  There are at
 * least a dozen different formulas used to define quantiles; we pick this
 * version as being the most "natural" since it
 * (a) interpolates linearly between adjacent points, and
 * (b) satisfies the condition that
 *     E[quantile(S, x) | x uniform in (0, 1)] = sum(S) / |S|
 *
 * For input values S_0 <= S_1 <= ... <= S_{n-1}, the quantile function is
 * defined as:
 *     quantile(S, q / n) = S_0			if q < 1/2
 *     quantile(S, (i + 1/2) / n) = S_i		for 0 <= i <= n-1
 *     quantile(S, q / n) = S_{n-1}		if q > n - 1/2
 * with linear interpolation between adjacent points in the range between
 * points listed above.
 */

/**
 * hazenquantile(N, x, i, r):
 * Set values ${i} and ${r} so that for |S| = N,
 *     quantile(S, x) = S_i + r * (S_{i+1} - S_i)
 * and i + r <= N - 1, 0 <= r < 1.  The inputs must satisfy 0 < N, 0 <= x <= 1.
 */
static inline void
hazenquantile(size_t N, double x, size_t * i, double * r)
{
	double q;

	/* Sanity-check. */
	assert(N > 0);
	assert((0 <= x) && (x <= 1));

	/* Scale to [0, N]. */
	q = (double)(N) * x;

	/* In most cases, i is equal to q rounded to nearest, minus one. */
	*i = (size_t)(q + 0.5) - 1;

	/* In most cases, r is the fractional part of q + 0.5. */
	*r = (q + 0.5) - (double)(*i + 1);

	/* The endpoints need special treatment. */
	if (*i == (size_t)(-1)) {
		/* In the case q < 0.5, quantile = S_0. */
		*i = 0;
		*r = 0.0;
	} else if (*i == N - 1) {
		/* In the case q > N - 0.5, quantile = S_{N-1}. */
		*i = N - 1;
		*r = 0.0;
	}

	/* Sanity-check. */
	assert((*i + *r < N));
	assert((0.0 <= *r) && (*r < 1.0));
}

#endif /* !_HAZENQUANTILE_H_ */

#ifndef _ONLINEQUANTILE_H_
#define _ONLINEQUANTILE_H_

#include <stddef.h>

/* Opaque quantile-computation type. */
struct onlinequantile;

/**
 * onlinequantile_init(q):
 * For 0 <= ${q} <= 1, prepare to compute (online) quantiles of doubles.
 */
struct onlinequantile * onlinequantile_init(double);

/**
 * onlinequantile_create(S, N, q):
 * For 0 <= ${q} <= 1, prepare to compute (online) quantiles of doubles, and
 * initialize with the ${N} values in ${S}. This is faster than creating an
 * empty structure and adding the elements individually.
 */
struct onlinequantile * onlinequantile_create(const double *, size_t, double);

/**
 * onlinequantile_get(Q, x):
 * Set *${x} to the current quantile value from the structure ${Q} and
 * return 0.  If there is no data, return 1.
 */
int onlinequantile_get(struct onlinequantile *, double *);

/**
 * onlinequantile_add(Q, x):
 * Add the value ${x} to the quantile structure ${Q}.
 */
int onlinequantile_add(struct onlinequantile *, double);

/**
 * onlinequantile_free(Q):
 * Free the quantile structure ${Q}.
 */
void onlinequantile_free(struct onlinequantile *);

#endif /* !_ONLINEQUANTILE_H_ */

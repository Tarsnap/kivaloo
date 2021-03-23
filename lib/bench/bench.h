#ifndef _BENCH_H_
#define _BENCH_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque structure. */
struct bench;

/**
 * bench_init(start, num_seconds):
 * Allocate and initialize the benchmark timing structure.
 */
struct bench * bench_init(size_t, size_t);

/**
 * bench_tick(B, done):
 * Increment the count and check the time.  If benchmarking should stop,
 * set ${done} to 1.
 */
int bench_tick(struct bench *, int *);

/**
 * bench_get_ticks(B):
 * Get the array containing the number of ticks per second.  The callee is
 * responsible for knowing the length of the array.
 */
uint64_t * bench_get_ticks(struct bench *);

/**
 * bench_mean(B):
 * Return the mean number of ticks per second during the benchmark period.
 */
uint64_t bench_mean(struct bench *);

/**
 * bench_median(B):
 * Return the median number of ticks per second.
 */
uint64_t bench_median(struct bench *);

/**
 * bench_free(B):
 * Free the cookie ${B}.
 */
void bench_free(struct bench *);

#endif /* !_BENCH_H_ */

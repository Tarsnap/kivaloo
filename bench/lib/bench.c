#include <sys/time.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "monoclock.h"
#include "warnp.h"

#include "bench.h"

struct bench {
	/* User-specified. */
	size_t num_seconds;

	/* Benchmark data. */
	uint64_t * ticks;
	uint64_t * sorted;
	size_t tick_pos;

	/* Internal time-keeping. */
	struct timeval tv_start;
	struct timeval tv_end;
	int started;
};

/**
 * bench_init(start, num_seconds):
 * Allocate and initialize the benchmark timing structure.
 */
struct bench *
bench_init(size_t start, size_t num_seconds)
{
	struct bench * B;
	struct timeval tv_now;

	/* Sanity checks. */
	assert((start + num_seconds) < INT_MAX);
	assert(num_seconds > 0);

	/* Allocate and initialize cookie. */
	if ((B = malloc(sizeof(struct bench))) == NULL) {
		warn0("malloc");
		goto err0;
	}
	B->num_seconds = num_seconds;
	B->started = 0;

	/* Allocate and initialize tick counts. */
	if ((B->ticks = malloc(num_seconds * sizeof(uint64_t))) == NULL) {
		warn0("malloc");
		goto err1;
	}
	if ((B->sorted = malloc(num_seconds * sizeof(uint64_t))) == NULL) {
		warn0("malloc");
		goto err2;
	}
	memset(B->ticks, 0, num_seconds * sizeof(uint64_t));
	B->tick_pos = 0;

	/* Get current time. */
	if (monoclock_get(&tv_now)) {
		warnp("monoclock_get");
		goto err3;
	}

	/* Prepare start benchmark time value. */
	B->tv_start.tv_sec = tv_now.tv_sec + (int)start;
	B->tv_start.tv_usec = tv_now.tv_usec;

	/* Success! */
	return (B);

err3:
	free(B->sorted);
err2:
	free(B->ticks);
err1:
	free(B);
err0:
	/* Failure! */
	return (NULL);
}

/* Compare timevals. */
static int tv_cmp(const struct timeval a, const struct timeval b)
{

	if (a.tv_sec < b.tv_sec)
		return (-1);
	else if (a.tv_sec > b.tv_sec)
		return (1);
	else if (a.tv_usec < b.tv_usec)
		return (-1);
	else if (a.tv_usec > b.tv_usec)
		return (1);
	else
		return (0);
}

/**
 * bench_tick(B, done):
 * Increment the count and check the time.  If benchmarking should stop,
 * set ${done} to 1.
 */
int
bench_tick(struct bench * B, int * done)
{
	struct timeval tv_now;

	/* Get current time. */
	if (monoclock_get(&tv_now)) {
		warnp("monoclock_get");
		goto err0;
	}

	/* Are we still waiting to start recording ticks? */
	if (B->started == 0) {
		/* Bail if it's not time to start counting items. */
		if (tv_cmp(tv_now, B->tv_start) < 0)
			goto done;

		/* Start recording ticks. */
		B->started = 1;

		/* Start tracking seconds. */
		B->tv_end.tv_sec = tv_now.tv_sec + 1;
		B->tv_end.tv_usec = tv_now.tv_usec;
	}

	/* Have we finished a second? */
	if (tv_cmp(tv_now, B->tv_end) > 0) {
		/* Prepare to record in a new position. */
		B->tick_pos++;

		/* Have we run out of seconds to record? */
		if (B->tick_pos >= B->num_seconds) {
			*done = 1;
			goto done;
		}

		/* Set up the next second. */
		B->tv_end.tv_sec = tv_now.tv_sec + 1;
		B->tv_end.tv_usec = tv_now.tv_usec;
	}

	/* Record value in the current position. */
	B->ticks[B->tick_pos]++;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * bench_get_ticks(B):
 * Get the array containing the number of ticks per second.  The callee is
 * responsible for knowing the length of the array.
 */
uint64_t *
bench_get_ticks(struct bench * B)
{

	return (B->ticks);
}

/**
 * bench_mean(B):
 * Return the mean number of ticks per second during the benchmark period.
 */
uint64_t
bench_mean(struct bench * B)
{
	size_t i;
	uint64_t sum = 0;

	/* Calculate sum. */
	for (i = 0; i < B->num_seconds; i++)
		sum += B->ticks[i];

	/* Return the mean. */
	return (sum / B->num_seconds);
}

static int
compar_uint64_t(const void * ap, const void * bp)
{
	const uint64_t a = *(const uint64_t *)ap;
	const uint64_t b = *(const uint64_t *)bp;

	if (a > b)
		return (1);
	else if (b > a)
		return (-1);
	else
		return (0);
}

/**
 * bench_median(B):
 * Return the median number of ticks per second.
 */
uint64_t
bench_median(struct bench * B)
{
	uint64_t median;
	size_t midpos;

	/* Make a copy of the ticks. */
	memcpy(B->sorted, B->ticks, B->num_seconds * sizeof(uint64_t));

	/* Sort the extra array. */
	qsort(B->sorted, B->num_seconds, sizeof(uint64_t), compar_uint64_t);

	/* Find the midpoint, or mean of nearest midpoints. */
	midpos = (B->num_seconds - 1) / 2;
	if ((B->num_seconds % 2) == 0)
		median = (B->sorted[midpos] + B->sorted[midpos + 1]) / 2;
	else
		median = B->sorted[midpos];

	/* Return the median. */
	return (median);
}

/**
 * bench_free(B):
 * Free the cookie ${B}.
 */
void
bench_free(struct bench * B)
{

	/* Behave consistently with free(NULL). */
	if (B == NULL)
		return;

	free(B->sorted);
	free(B->ticks);
	free(B);
}

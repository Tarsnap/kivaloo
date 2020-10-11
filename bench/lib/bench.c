#include <sys/time.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include "monoclock.h"
#include "warnp.h"

#include "bench.h"

struct bench {
	/* User-specified. */
	size_t num_seconds;

	/* Benchmark data. */
	uint64_t ticks;

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

	/* Allocate and initialize cookie. */
	if ((B = malloc(sizeof(struct bench))) == NULL) {
		warn0("malloc");
		goto err0;
	}
	B->num_seconds = num_seconds;
	B->ticks = 0;
	B->started = 0;

	/* Get current time. */
	if (monoclock_get(&tv_now)) {
		warnp("monoclock_get");
		goto err1;
	}

	/* Prepare benchmark time values. */
	B->tv_start.tv_sec = tv_now.tv_sec + (int)start;
	B->tv_start.tv_usec = tv_now.tv_usec;
	B->tv_end.tv_sec = tv_now.tv_sec + (int)(start + num_seconds);
	B->tv_end.tv_usec = tv_now.tv_usec;

	/* Success! */
	return (B);

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
	}

	/* Have we finished? */
	if (tv_cmp(tv_now, B->tv_end) > 0) {
		*done = 1;
		goto done;
	}

	/* Record value. */
	B->ticks++;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * bench_mean(B):
 * Return the mean number of ticks per second during the benchmark period.
 */
uint64_t
bench_mean(struct bench * B)
{

	/* Return the mean. */
	return (B->ticks / B->num_seconds);
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

	free(B);
}

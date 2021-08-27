#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "asprintf.h"
#include "elasticarray.h"
#include "events.h"
#include "logging.h"
#include "onlinequantile.h"

#include "reqtypes.h"

#include "perfstats.h"

/* Statistics for a single request type. */
struct reqstats {
	uint32_t reqtype;
	size_t N;
	double mu;
	struct onlinequantile * p50;
	struct onlinequantile * p99;
	double p100;
};

ELASTICARRAY_DECL(REQSTATSLIST, reqstatslist, struct reqstats *);
ELASTICARRAY_DECL(STR, str, char);

/* Performance tracking state. */
struct perfstats {
	struct logging_file * L;
	long secsperreport;
	time_t lastreport;
	void * timer_cookie;
	REQSTATSLIST stats;
};

/* Print statistics. */
static int
flush(struct perfstats * P)
{
	STR logline;
	struct reqstats * r;
	struct reqstats ** rs;
	double p50, p99;
	char * s;
	size_t linelen;
	size_t nrs;
	size_t i, j;

	/* Export statistics from the elastic array. */
	if (reqstatslist_exportdup(P->stats, &rs, &nrs))
		goto err0;

	/* Sort the statistics by request type. */
	for (i = 0; i < nrs; i++) {
		for (j = i + 1; j < nrs; j++) {
			if (rs[i]->reqtype > rs[j]->reqtype) {
				r = rs[i];
				rs[i] = rs[j];
				rs[j] = r;
			}
		}
	}

	/* Gather statistics for each request type. */
	if ((logline = str_init(0)) == NULL)
		goto err1;
	for (i = 0; i < nrs; i++) {
		r = rs[i];
		onlinequantile_get(r->p50, &p50);
		onlinequantile_get(r->p99, &p99);
		if (asprintf(&s, "|%s|%06zu|%08.3f|%08.3f|%08.3f|%08.3f",
		    reqtypes_lookup(r->reqtype), r->N, 1000 * r->mu,
		        1000 * p50, 1000 * p99, 1000 * r->p100) == -1)
			goto err2;
		if (str_append(logline, s, strlen(s)))
			goto err3;
		free(s);
	}
	if (str_append(logline, "\0", 1))
		goto err2;
	if (str_export(logline, &s, &linelen))
		goto err2;
	logging_printf(P->L, "%s", s);
	free(s);

	/* Free the statistics and empty the elastic array. */
	for (i = 0; i < nrs; i++) {
		r = rs[i];
		onlinequantile_free(r->p99);
		onlinequantile_free(r->p50);
		free(r);
	}
	free(rs);
	reqstatslist_resize(P->stats, 0);

	/* Success! */
	return (0);

err3:
	free(s);
err2:
	str_free(logline);
err1:
	free(rs);
err0:
	/* Failure! */
	return (-1);
}

/* Compute time, rounded down to nearest w seconds. */
static int
timetrunc(time_t * t, long w)
{

	/* Get the current time. */
	if (time(t) == (time_t)(-1))
		goto err0;

	/* Round down. */
	*t = (time_t)((long)(*t / w) * w);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Flush statistics if needed. */
static int
poke(struct perfstats * P)
{
	time_t t_now;

	/* Get the current time. */
	if (timetrunc(&t_now, P->secsperreport))
		goto err0;

	/* Is it time to flush statistics? */
	if (t_now != P->lastreport) {
		if (flush(P))
			goto err0;
		P->lastreport = t_now;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Timer tick: Check if we need to flush statistics. */
static int
callback_timer(void * cookie)
{
	struct perfstats * P = cookie;

	/* We are no longer waiting for this callback. */
	P->timer_cookie = NULL;

	/* Check if we need to flush statistics. */
	if (poke(P))
		goto err0;

	/* Schedule another callback. */
	if ((P->timer_cookie = events_timer_register_double(callback_timer,
	    P, 0.5)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * perfstats_init(L, w):
 * Prepare for collecting and logging performance statistics.  Return a cookie
 * which can be passed to perfstats_add and perfstats_done; and every ${w}
 * seconds, log output to ${L}.
 */
struct perfstats *
perfstats_init(struct logging_file * L, long w)
{
	struct perfstats * P;

	/* Bake a cookie. */
	if ((P = malloc(sizeof(struct perfstats))) == NULL)
		goto err0;
	P->L = L;
	P->secsperreport = w;
	if ((P->stats = reqstatslist_init(0)) == NULL)
		goto err1;

	/* Statistics prior to now have been reported. */
	if (timetrunc(&P->lastreport, P->secsperreport))
		goto err2;

	/* Start a timer. */
	if ((P->timer_cookie = events_timer_register_double(callback_timer,
	    P, 0.5)) == NULL)
		goto err2;

	/* Success! */
	return (P);

err2:
	reqstatslist_free(P->stats);
err1:
	free(P);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * perfstats_add(P, reqtype, t):
 * Record that a request of type ${reqtype} took ${t} seconds to return.
 */
int
perfstats_add(struct perfstats * P, uint32_t reqtype, double t)
{
	struct reqstats * r;
	size_t i;

	/* Flush statistics if appropriate. */
	if (poke(P))
		goto err0;

	/* Do we have statistics for this request type yet? */
	for (i = 0; i < reqstatslist_getsize(P->stats); i++) {
		r = *reqstatslist_get(P->stats, i);
		if (r->reqtype == reqtype)
			break;
	}

	/* If not, allocate a new structure and add it. */
	if (i == reqstatslist_getsize(P->stats)) {
		/* Allocate a new structure. */
		if ((r = malloc(sizeof(struct reqstats))) == NULL)
			goto err0;
		r->reqtype = reqtype;
		r->N = 0;
		r->mu = 0.0;
		if ((r->p50 = onlinequantile_init(0.50)) == NULL)
			goto err1;
		if ((r->p99 = onlinequantile_init(0.99)) == NULL)
			goto err2;
		r->p100 = 0.0;

		/* Add to the elastic array. */
		if (reqstatslist_append(P->stats, &r, 1))
			goto err3;
	}

	/* Add to the statistics for this request type. */
	r = *reqstatslist_get(P->stats, i);
	r->N++;
	r->mu += (t - r->mu) / (double)r->N;
	if (onlinequantile_add(r->p50, t))
		goto err0;
	if (onlinequantile_add(r->p99, t))
		goto err0;
	if (r->p100 < t)
		r->p100 = t;

	/* Success! */
	return (0);

err3:
	onlinequantile_free(r->p99);
err2:
	onlinequantile_free(r->p50);
err1:
	free(r);
err0:
	/* Failure! */
	return (-1);
}

/**
 * perfstats_done(P):
 * Log final statistics and free the performance statistics cookie ${P}.  On
 * error, the statistics may have not been written but the cookie will still
 * have been freed.
 */
int
perfstats_done(struct perfstats * P)
{
	int rc;

	/* Flush any unreported statistics. */
	rc = flush(P);

	/* Cancel the timer if we're waiting for a callback. */
	if (P->timer_cookie) {
		events_timer_cancel(P->timer_cookie);
		P->timer_cookie = NULL;
	}

	/* Free our state. */
	reqstatslist_free(P->stats);
	free(P);

	/* Return code from flush(). */
	return (rc);
}

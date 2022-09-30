#ifndef PERFSTATS_H_
#define PERFSTATS_H_

#include <stdint.h>

/* Opaque type. */
struct logging_file;
struct perfstats;

/**
 * perfstats_init(L, w):
 * Prepare for collecting and logging performance statistics.  Return a cookie
 * which can be passed to perfstats_add and perfstats_done; and every ${w}
 * seconds, log output to ${L}.
 */
struct perfstats * perfstats_init(struct logging_file *, long);

/**
 * perfstats_add(P, reqtype, t):
 * Record that a request of type ${reqtype} took ${t} seconds to return.
 */
int perfstats_add(struct perfstats *, uint32_t, double);

/**
 * perfstats_done(P):
 * Log final statistics and free the performance statistics cookie ${P}.  On
 * error, the statistics may have not been written but the cookie will still
 * have been freed.
 */
int perfstats_done(struct perfstats *);

#endif /* !PERFSTATS_H_ */

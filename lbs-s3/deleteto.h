#ifndef _DELETETO_H_
#define _DELETETO_H_

#include <stdint.h>

/* Opaque type. */
struct wire_requestqueue;

/**
 * deleteto_init(Q_S3, bucket):
 * Initialize the deleter to operate on bucket ${bucket} via the S3 daemon
 * connected to ${Q_S3}.  This function may call events_run() internally.
 */
struct deleteto * deleteto_init(struct wire_requestqueue *, const char *);

/**
 * deleteto_deleteto(D, N):
 * S3 objects with numbers less than ${N} are no longer needed by the B+Tree.
 * Inform the deleteto state ${D} which may opt to do something about them.
 */
int deleteto_deleteto(struct deleteto *, uint64_t);

/**
 * deleteto_stop(D):
 * Clean up, shut down, and free the deleteto state ${D}.  This function may
 * call events_run() internally.
 */
int deleteto_stop(struct deleteto *);

#endif /* !_DELETETO_H_ */

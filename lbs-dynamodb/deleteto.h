#ifndef _DELETETO_H_
#define _DELETETO_H_

#include <stdint.h>

/* Opaque type. */
struct wire_requestqueue;

/**
 * deleteto_init(Q_DDBKV):
 * Initialize the deleter to operate via the DynamoDB-KV daemon connected to
 * ${Q_DDBKV}.  This function may call events_run internally.
 */
struct deleteto * deleteto_init(struct wire_requestqueue *);

/**
 * deleteto_deleteto(D, N):
 * Pages with numbers less than ${N} are no longer needed by the B+Tree.
 * Inform the deleteto state ${D} which may opt to do something about them.
 */
int deleteto_deleteto(struct deleteto *, uint64_t);

/**
 * deleteto_stop(D):
 * Clean up, shut down, and free the deleteto state ${D}.  This function may
 * call events_run internally.
 */
int deleteto_stop(struct deleteto *);

#endif /* !_DELETETO_H_ */

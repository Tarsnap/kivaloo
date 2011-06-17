#ifndef _WORKER_H_
#define _WORKER_H_

#include <stdint.h>

#include "storage.h"

/* Opaque thread control structure. */
struct workctl;

/**
 * worker_create(ID, sstate, wakeupsock):
 * Create a worker thread which performs operations on the storage state
 * ${sstate} and writes the ID ${ID} to the socket ${wakeupsock} when each
 * operation is done.
 */
struct workctl * worker_create(size_t, struct storage_state *, int);

/**
 * worker_assign(ctl, op, blkno, nblks, buf, reqID):
 * Assign the work tuple (${op}, ${blkno}, ${nblks}, ${buf}, ${reqID}) to the
 * thread with work control structure ${ctl} and wake it up.
 */
int worker_assign(struct workctl *,
    int, uint64_t, size_t, uint8_t *, uint64_t);

/**
 * worker_getdone(ctl, op, blkno, nblks, buf, reqID):
 * Set (${op}, ${blkno}, ${nblks}, ${buf}, ${reqID}) to the work tuple which
 * has been completed by the thread with work control structure ${ctl}, and
 * mark the thread as having no work.
 */
int worker_getdone(struct workctl *,
    int *, uint64_t *, size_t *, uint8_t **, uint64_t *);

/**
 * worker_kill(ctl):
 * Tell the thread with control structure ${ctl} to die and clean it up.
 */
int worker_kill(struct workctl *);

#endif /* !_WORKER_H_ */

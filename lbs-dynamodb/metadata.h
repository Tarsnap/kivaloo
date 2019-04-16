#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

/* Opaque type. */
struct wire_requestqueue;
struct metadata;

/**
 * metadata_init(Q):
 * Prepare for metadata operations using the queue ${Q}.  This function may
 * call events_run internally.
 */
struct metadata * metadata_init(struct wire_requestqueue *);

/**
 * metadata_nextblk_read(M, nextblk):
 * Read the "nextblk" value.
 */
int metadata_nextblk_read(struct metadata *, uint64_t *);

/**
 * metadata_nextblk_write(M, nextblk, callback, cookie):
 * Store "nextblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_nextblk_write(struct metadata *, uint64_t,
    int (*)(void *, int), void *);

/**
 * metadata_deletedto_read(M, deletedto):
 * Read the "deletedto" value.
 */
int metadata_deletedto_read(struct metadata *, uint64_t *);

/**
 * metadata_deletedto_write(M, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_deletedto_write(struct metadata *, uint64_t,
    int (*)(void *, int), void *);

/**
 * metadata_free(M):
 * Stop metadata operations.
 */
void metadata_free(struct metadata *);

#endif /* !_METADATA_H_ */

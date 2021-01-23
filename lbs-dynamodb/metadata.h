#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

/* Opaque type. */
struct wire_requestqueue;
struct metadata;

/**
 * metadata_init(Q):
 * Prepare for metadata operations using the queue ${Q}.  This function may
 * call events_run() internally.
 */
struct metadata * metadata_init(struct wire_requestqueue *);

/**
 * metadata_nextblk_read(M):
 * Return the "nextblk" value.
 */
uint64_t metadata_nextblk_read(struct metadata *);

/**
 * metadata_nextblk_write(M, nextblk, callback, cookie):
 * Store "nextblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_nextblk_write(struct metadata *, uint64_t,
    int (*)(void *), void *);

/**
 * metadata_lastblk_read(M):
 * Return the "lastblk" value.
 */
uint64_t metadata_lastblk_read(struct metadata *);

/**
 * metadata_lastblk_write(M, lastblk, callback, cookie):
 * Store "lastblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_lastblk_write(struct metadata *, uint64_t,
    int (*)(void *), void *);

/**
 * metadata_deletedto_read(M):
 * Return the "deletedto" value.
 */
uint64_t metadata_deletedto_read(struct metadata *);

/**
 * metadata_deletedto_write(M, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_deletedto_write(struct metadata *, uint64_t,
    int (*)(void *), void *);

/**
 * metadata_free(M):
 * Stop metadata operations.
 */
void metadata_free(struct metadata *);

#endif /* !_METADATA_H_ */

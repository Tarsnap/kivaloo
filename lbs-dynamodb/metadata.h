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
 * metadata_deletedto_write(M, deletedto):
 * Store "deletedto" value.
 */
void metadata_deletedto_write(struct metadata *, uint64_t);

/**
 * metadata_deletedto_register(M, callback, cookie):
 * Register ${callback}(${cookie}) to be called every time metadata is stored.
 * This API exists for the benefit of the deletedto code; only one callback can
 * be registered in this manner at once, and the callback must be reset to NULL
 * before metadata_free is called.
 */
void metadata_deletedto_register(struct metadata *, int(*)(void *), void *);

/**
 * metadata_flush(M):
 * Trigger a flush of pending metadata updates.
 */
int metadata_flush(struct metadata *);

/**
 * metadata_free(M):
 * Stop metadata operations.
 */
void metadata_free(struct metadata *);

#endif /* !_METADATA_H_ */

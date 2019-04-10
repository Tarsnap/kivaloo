#ifndef _METADATA_H_
#define _METADATA_H_

#include <stdint.h>

/* Opaque type. */
struct wire_requestqueue;

/**
 * metadata_lastblk_read(Q, lastblk):
 * Read the "lastblk" value.  This function may call events_run internally.
 */
int metadata_lastblk_read(struct wire_requestqueue *, uint64_t *);

/**
 * metadata_lastblk_write(Q, lastblk, callback, cookie):
 * Store "lastblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_lastblk_write(struct wire_requestqueue *, uint64_t,
    int (*)(void *, int), void *);

/**
 * metadata_deletedto_read(Q, lastblk):
 * Read the "deletedto" value.  This function may call events_run internally.
 */
int metadata_deletedto_read(struct wire_requestqueue *, uint64_t *);

/**
 * metadata_deletedto_write(Q, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int metadata_deletedto_write(struct wire_requestqueue *, uint64_t,
    int (*)(void *, int), void *);

#endif /* !_METADATA_H_ */

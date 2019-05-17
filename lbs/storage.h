#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque structure holding back-end storage state. */
struct storage_state;

/**
 * storage_init(storagedir, blklen, latency, nosync):
 * Initialize and return the storage state for ${blklen}-byte blocks of data
 * stored in ${storagedir}.  Sleep ${latency} ns in storage_read calls.  If
 * ${nosync} is non-zero, don't use fsync.
 */
struct storage_state * storage_init(const char *, size_t, long, int);

/**
 * storage_nextblock(S):
 * Return the next writable block number for storage state ${S}, or
 * (uint64_t)(-1) on error.
 */
uint64_t storage_nextblock(struct storage_state *);

/**
 * storage_read(S, blkno, buf):
 * Using storage state ${S}, read block number ${blkno} into the buffer
 * ${buf}.  Return 1 on success; 0 if the block does not exist; or
 * (uint64_t)(-1) on error.
 */
uint64_t storage_read(struct storage_state *, uint64_t, uint8_t *);

/**
 * storage_write(S, blkno, nblks, buf):
 * Using storage state ${S}, append ${nblks} blocks from ${buf} starting at
 * block ${blkno}.  There MUST NOT at any time be more than one thread
 * calling this function.
 */
int storage_write(struct storage_state *, uint64_t, uint64_t, uint8_t *);

/**
 * storage_delete(S, blkno):
 * Using storage state ${S}, delete none, some, or all blocks prior to (but
 * not including) block ${blkno}.
 */
int storage_delete(struct storage_state *, uint64_t);

/**
 * storage_done(S):
 * Free the storage state data ${S}.
 */
int storage_done(struct storage_state *);

#endif /* !_STORAGE_H_ */

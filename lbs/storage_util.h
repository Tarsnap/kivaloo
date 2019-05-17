#ifndef _STORAGE_UTIL_H_
#define _STORAGE_UTIL_H_

#include <stdint.h>

/* Opaque types. */
struct storage_state;

/**
 * storage_util_readlock(S):
 * Grab a read lock on the storage state ${S}.
 */
int storage_util_readlock(struct storage_state *);

/**
 * storage_util_writelock(S):
 * Grab a write lock on the storage state ${S}.
 */
int storage_util_writelock(struct storage_state *);

/**
 * storage_util_unlock(S):
 * Release the lock on the storage state ${S}.
 */
int storage_util_unlock(struct storage_state *);

/**
 * storage_util_mkpath(S, fileno):
 * Return the malloc-allocated NUL-terminated string "${dir}/blks_${fileno}"
 * where ${dir} is ${S}->storagedir and ${fileno} is a 0-padding hex value.
 */
char * storage_util_mkpath(struct storage_state *, uint64_t);

#endif /* !_STORAGE_UTIL_H_ */

#ifndef STORAGE_INTERNAL_H_
#define STORAGE_INTERNAL_H_

#include <pthread.h>
#include <stdint.h>

/* Opaque types. */
struct elasticqueue;

/* Back-end storage state. */
struct storage_state {
	/* Static data. */
	const char * storagedir;	/* Directory containing bits. */
	size_t blocklen;		/* Block size in bytes. */
	uint64_t maxnblks;		/* Maximum # of blocks in a file. */

	/* Debugging options. */
	long latency;			/* Read latency in ns. */
	int nosync;			/* Don't sync to disk. */

	/* Dynamic data. */
	pthread_rwlock_t lck;		/* Lock on dynamic data. */
	struct elasticqueue * files;	/* File states. */
	uint64_t minblk;		/* Minimum valid block #. */
	uint64_t nextblk;		/* Next block # to write. */
};

/**
 * The following invariants hold whenever lck is not held for writing:
 * 1. If files is empty, minblk == nextblk == 0.
 * 2. If files is non-empty, minblk = head(files)->start.
 * 3. If files is non-empty, nextblk = tail(files)->start + tail(files)->len.
 * 4. For consecutive entries x, y in files, x->start + x->len = y->start.
 */

#endif /* !STORAGE_INTERNAL_H_ */

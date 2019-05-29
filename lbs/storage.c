#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "elasticqueue.h"
#include "warnp.h"

#include "disk.h"
#include "storage_findfiles.h"
#include "storage_internal.h"
#include "storage_util.h"

#include "storage.h"

/* State of an individual file. */
struct file_state {
	uint64_t start;			/* First block # in file. */
	uint64_t len;			/* Length of file in blocks. */
};

/**
 * storage_init(storagedir, blklen, latency, nosync):
 * Initialize and return the storage state for ${blklen}-byte blocks of data
 * stored in ${storagedir}.  Sleep ${latency} ns in storage_read calls.  If
 * ${nosync} is non-zero, don't use fsync.
 */
struct storage_state *
storage_init(const char * storagedir, size_t blocklen, long latency,
    int nosync)
{
	struct storage_state * S;
	struct elasticqueue * files;
	struct storage_file * sf;
	struct file_state fs;
	char * s;
	int rc;

	/* Sanity-check the block size. */
	assert(blocklen > 0);

	/* Allocate structure and fill in static data. */
	if ((S = malloc(sizeof(struct storage_state))) == NULL)
		goto err0;
	S->storagedir = storagedir;
	S->blocklen = blocklen;
	S->latency = latency;
	S->nosync = nosync;

	/*
	 * Figure out the maximum number of blocks a file can contain without
	 * having its length overflow either uint64_t or off_t.
	 */
#ifdef OFF_MAX
	S->maxnblks = UINT64_MAX;
	if (S->maxnblks > OFF_MAX)
		S->maxnblks = OFF_MAX;
#else
	/* Just treat off_t as an int32_t. */
	assert((int32_t)(off_t)(INT32_MAX) == INT32_MAX);
	S->maxnblks = INT32_MAX;
#endif
	S->maxnblks = S->maxnblks / S->blocklen;

	/* Create an elastic queue to hold block file state. */
	if ((S->files = elasticqueue_init(sizeof(struct file_state))) == NULL)
		goto err1;

	/* Get a sorted list of block files. */
	if ((files = storage_findfiles(S->storagedir)) == NULL)
		goto err2;

	/* If we have at least one file, its # is where the blocks start. */
	if (elasticqueue_getlen(files) > 0) {
		sf = elasticqueue_get(files, 0);
		S->minblk = sf->fileno;
	} else {
		/* Otherwise, we have nothing. */
		S->minblk = 0;
	}

	/* Look at files one by one. */
	S->nextblk = S->minblk;
	while (elasticqueue_getlen(files) > 0) {
		/* Grab a file. */
		sf = elasticqueue_get(files, 0);

		/* The first block is the file number. */
		fs.start = sf->fileno;

		/* Is the first block the one we expected? */
		if (fs.start != S->nextblk) {
			warn0("Start of block storage file does not match"
			    " end of previous file: %016" PRIx64, sf->fileno);
			goto err3;
		}

		/* Does it have a non-integer number of blocks? */
		if ((sf->len % S->blocklen) != 0) {
			/* Not permitted for files in the middle. */
			if (elasticqueue_getlen(files) > 1) {
				warn0("Block storage file has non-integer"
				    " number of blocks: %016" PRIx64,
				    sf->fileno);
				goto err3;
			}

			/*
			 * The final file may have a non-integer number of
			 * blocks due to an interrupted write; just remove
			 * any partial block.
			 */
			if ((s = storage_util_mkpath(S, sf->fileno)) == NULL)
				goto err3;
			if (truncate(s, sf->len - (sf->len % S->blocklen)))
				goto err4;
			free(s);
		}

		/* Compute number of blocks. */
		fs.len = sf->len / S->blocklen;

		/* Add to the queue of block file state structures. */
		if (elasticqueue_add(S->files, &fs))
			goto err3;

		/* Adjust nextblk to account for this latest block file. */
		S->nextblk = fs.start + fs.len;

		/* Remove the file from the queue. */
		elasticqueue_delete(files);
	}

	/* Free the (now empty) queue of files. */
	elasticqueue_free(files);

	/* Create a lock on the dynamic data. */
	if ((rc = pthread_rwlock_init(&S->lck, NULL)) != 0) {
		warn0("pthread_rwlock_init: %s", strerror(rc));
		goto err2;
	}

	/* Success! */
	return (S);

err4:
	free(s);
err3:
	elasticqueue_free(files);
err2:
	elasticqueue_free(S->files);
err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * storage_nextblock(S):
 * Return the next writable block number, or (uint64_t)(-1) on error.
 */
uint64_t
storage_nextblock(struct storage_state * S)
{
	uint64_t nextblk;

	/* Grab a read lock. */
	if (storage_util_readlock(S))
		goto err0;

	/* Grab the next block # value. */
	nextblk = S->nextblk;

	/* Release the lock. */
	if (storage_util_unlock(S))
		goto err0;

	/* Success! */
	return (nextblk);

err0:
	/* Failure! */
	return ((uint64_t)(-1));
}

/**
 * storage_read(S, blkno, buf):
 * Using storage state ${S}, read block number ${blkno} into the buffer
 * ${buf}.  Return 1 on success; 0 if the block does not exist; or
 * -1 on error.
 */
int
storage_read(struct storage_state * S, uint64_t blkno, uint8_t * buf)
{
	struct file_state * fs;
	size_t i;
	char * s = NULL;	/* free(NULL) simplifies error path. */
	struct timespec nstime;

	/* Grab a read lock. */
	if (storage_util_readlock(S))
		goto err0;

	/* Figure out if we have this block. */
	if ((blkno < S->minblk) || (blkno >= S->nextblk))
		goto enoent2;

	/* Figure out which file to read from, and at what position. */
	for (i = 0; ; i++) {
		fs = elasticqueue_get(S->files, i);
		assert(fs != NULL);
		if (blkno < fs->start + fs->len)
			break;
	}
	assert(fs->start <= blkno);

	/* Release the read lock. */
	if (storage_util_unlock(S))
		goto err0;

	/* Read the block. */
	if ((s = storage_util_mkpath(S, fs->start)) == NULL)
		goto err0;
	if (disk_read(s, (off_t)((blkno - fs->start) * S->blocklen),
	    S->blocklen, buf)) {
		/*
		 * If errno is ENOENT, we lost a race against the deleter
		 * thread.  The block does not exist.
		 */
		if (errno == ENOENT)
			goto enoent1;

		/* Anything else is an error. */
		goto err1;
	}
	free(s);

	/* Sleep the indicated duration. */
	if (S->latency) {
		nstime.tv_sec = 0;
		nstime.tv_nsec = S->latency;
		nanosleep(&nstime, NULL);
	}

	/* Success! */
	return (1);

enoent2:
	/* Release the lock. */
	if (storage_util_unlock(S))
		goto err1;
enoent1:
	free(s);

	/* This block is not available. */
	return (0);

err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_write(S, blkno, nblks, buf):
 * Using storage state ${S}, append ${nblks} blocks from ${buf} starting at
 * block ${blkno}.  There MUST NOT at any time be more than one thread
 * calling this function.
 */
int
storage_write(struct storage_state * S,
    uint64_t blkno, uint64_t nblks, uint8_t * buf)
{
	struct file_state fs_new;
	struct file_state * fs;
	int newfile;
	uint64_t fnum;
	char * s = NULL;	/* free(NULL) simplifies error path. */

	/* Sanity checks.  We must have nblks * S->blocklen <= SIZE_MAX. */
	assert((nblks != 0) && (S->blocklen != 0));
	assert(nblks <= SIZE_MAX / S->blocklen);

	/* Pick up a write lock. */
	if (storage_util_writelock(S))
		goto err0;

	/* Sanity-check the write position. */
	if (blkno != S->nextblk) {
		warn0("Attempt to append data with wrong blkno");
		warn0("(%016" PRIx64 ", should be %016" PRIx64 ")",
		    blkno, S->nextblk);
		goto err2;
	}

	/* Get a pointer to the last file (or NULL if no files exist). */
	fs = elasticqueue_get(S->files, elasticqueue_getlen(S->files) - 1);

	/**
	 * Figure out if we should continue appending to the last file, or
	 * create a new file.  We start a new file if either of the following
	 * conditions apply:
	 * 1. We have no files yet.
	 * 2. The last file is more than 1/16 of the total stored data.
	 * 3. Adding to the last file will result in the file having too
	 *    many blocks.
	 */
	if (fs == NULL)
		newfile = 1;
	else if (fs->len > (S->nextblk - S->minblk) / 16)
		newfile = 1;
	else if (fs->len + nblks > S->maxnblks)
		newfile = 1;
	else
		newfile = 0;

	/* If we're creating a new file, add a new file_state to the queue. */
	if (newfile) {
		fs_new.start = blkno;
		fs_new.len = 0;
		fs = &fs_new;
		if (elasticqueue_add(S->files, fs))
			goto err2;
	}

	/* Record which file we're appending to. */
	fnum = fs->start;

	/* Release the lock. */
	if (storage_util_unlock(S))
		goto err0;

	/* Write the block(s) to the end of the file. */
	if ((s = storage_util_mkpath(S, fnum)) == NULL)
		goto err0;
	if (disk_write(s, newfile, (size_t)(S->blocklen * nblks), buf,
	    S->nosync)) {
		goto err1;
	}
	free(s);

	/* Make sure any file creation is flushed to disk. */
	if ((newfile) && (S->nosync == 0)) {
		if (disk_syncdir(S->storagedir))
			goto err0;
	}

	/* Pick up a write lock. */
	if (storage_util_writelock(S))
		goto err0;

	/*
	 * Get a pointer to the last file.  We must do this here since the
	 * queue may have been modified while we didn't have a lock.
	 */
	fs = elasticqueue_get(S->files, elasticqueue_getlen(S->files) - 1);

	/* Adjust block count. */
	fs->len += nblks;

	/* Adjust next-block-to-write value. */
	S->nextblk += nblks;

	/* Release the lock. */
	if (storage_util_unlock(S))
		goto err0;

	/* Success! */
	return (0);

err2:
	storage_util_unlock(S);
err1:
	free(s);
err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_delete(S, blkno):
 * Using storage state ${S}, delete none, some, or all blocks prior to (but
 * not including) block ${blkno}.
 */
int
storage_delete(struct storage_state * S, uint64_t blkno)
{
	struct file_state * fs;
	uint64_t fileno;
	char * s;

	/* Loop until we don't need to delete anything. */
	do {
		/*
		 * First, figure out if we need to delete a file; if we do,
		 * remove it from the file queue.
		 */

		/* Grab a write lock. */
		if (storage_util_writelock(S))
			goto err0;

		/* If we have less than 2 files, don't delete anything. */
		if (elasticqueue_getlen(S->files) < 2)
			break;

		/* If the first file has blocks we need, don't delete. */
		fs = elasticqueue_get(S->files, 0);
		if (fs->start + fs->len > blkno)
			break;

		/* We want to delete the first file. */
		fileno = fs->start;

		/* Remove the file from the file queue. */
		elasticqueue_delete(S->files);

		/* Adjust our first-block-held value. */
		fs = elasticqueue_get(S->files, 0);
		S->minblk = fs->start;

		/* Release the lock. */
		if (storage_util_unlock(S))
			goto err0;

		/*
		 * Delete the file.  We don't need to worry about racing
		 * against the writer, since we will never delete the last
		 * file; and racing against readers is handled by readers
		 * treating ENOENT properly.
		 */
		if ((s = storage_util_mkpath(S, fileno)) == NULL)
			goto err0;
		if (unlink(s)) {
			warnp("unlink(%s)", s);
			goto err1;
		}
		free(s);

		/* Make sure the file deletion is flushed to disk. */
		if (disk_syncdir(S->storagedir))
			goto err0;
	} while (1);

	/* Release the write lock. */
	if (storage_util_unlock(S))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(s);
err0:

	/* Failure! */
	return (-1);
}

/**
 * storage_done(S):
 * Free the storage state data ${S}.
 */
int
storage_done(struct storage_state * S)
{
	int rc;

	/* Destroy the lock on the storage state. */
	if ((rc = pthread_rwlock_destroy(&S->lck)) != 0) {
		warn0("pthread_rwlock_destroy: %s", strerror(rc));
		goto err0;
	}

	/* Free the queue of file state structures. */
	elasticqueue_free(S->files);

	/* Free the storage state. */
	free(S);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

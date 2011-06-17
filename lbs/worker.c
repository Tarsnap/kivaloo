#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "warnp.h"

#include "storage.h"

#include "worker.h"

/* Thread control structure. */
struct workctl {
	/* Thread management. */
	pthread_mutex_t mtx;	/* Controls access to this structure. */
	pthread_t thr;		/* Thread ID. */
	pthread_cond_t cv;	/* Work-management condition variable. */
	int haswork;		/* Has-work condition. */
	int workdone;		/* Has-finished-work condition. */
	int suicide;		/* Need-to-kill-ourself condition. */

	/* Static state data. */
	size_t ID;		/* ID of this thread. */
	int wakeupsock;		/* Write ID here when done. */
	struct storage_state * sstate;	/* Storage state. */

	/* Work to be done. */
	int op;			/* 0 = read, 1 = write, 2 = free. */
	uint64_t blkno;		/* Block to read, first block to write, */
				/* or first block to NOT delete. */
	size_t nblks;		/* Number of blocks to write. */
				/* Number of blocks successfully read. */
	uint8_t * buf;		/* Buffer to read/write into/from. */
	uint64_t reqID;		/* ID of request (not used by worker). */
};

/* Worker thread. */
static void *
workthread(void * cookie)
{
	struct workctl * ctl = cookie;
	int rc;

	/* Grab the mutex. */
	if ((rc = pthread_mutex_lock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_lock: %s", strerror(rc));
		exit(1);
	}

	/* Infinite loop doing work until told to suicide. */
	do {
		/*
		 * Sleep until we have work which we haven't done yet or
		 * we need to kill ourself.
		 */
		while (((ctl->haswork == 0) || (ctl->workdone != 0)) &&
		    (ctl->suicide == 0)) {
			/* Sleep until we're woken up. */
			if ((rc = pthread_cond_wait(&ctl->cv,
			    &ctl->mtx)) != 0) {
				warn0("pthread_cond_wait: %s", strerror(rc));
				exit(1);
			}
		}

		/* If we need to kill ourself, stop looping. */
		if (ctl->suicide)
			break;

		/* Do the work. */
		switch (ctl->op) {
		case 0:	/* Read */
			if ((ctl->nblks = storage_read(ctl->sstate,
			    ctl->blkno, ctl->buf)) == (size_t)(-1)) {
				warnp("Failure reading block");
				exit(1);
			}
			break;
		case 1:	/* Write */
			if (storage_write(ctl->sstate,
			    ctl->blkno, ctl->nblks, ctl->buf)) {
				warnp("Failure writing blocks");
				exit(1);
			}
			break;
		case 2:	/* Delete */
			if (storage_delete(ctl->sstate, ctl->blkno)) {
				warnp("Failure deleting blocks");
				exit(1);
			}
			break;
		default:
			warn0("Invalid op: %d", ctl->op);
		}

		/* We've done the work; notify the master thread. */
		ctl->workdone = 1;
		if (write(ctl->wakeupsock, &ctl->ID, sizeof(size_t)) <
		    (ssize_t)sizeof(size_t)) {
			warnp("Error writing to wakeup socket");
			exit(1);
		}
	} while (1);

	/* Release the mutex and die. */
	if ((rc = pthread_mutex_unlock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_unlock: %s", strerror(rc));
		exit(1);
	}
	return (NULL);
}

/**
 * worker_create(ID, sstate, wakeupsock):
 * Create a worker thread which performs operations on the storage state
 * ${sstate} and writes the ID ${ID} to the socket ${wakeupsock} when each
 * operation is done.
 */
struct workctl *
worker_create(size_t ID, struct storage_state * sstate, int wakeupsock)
{
	struct workctl * ctl;
	int rc;

	/* Allocate a worker structure. */
	if ((ctl = malloc(sizeof(struct workctl))) == NULL)
		goto err0;

	/*
	 * Create and lock mutex.  Locking the mutex here is arguably rather
	 * silly, since we're the only thread with access to the workctl
	 * structure right now; but theoretically pthread_create could launch
	 * a new thread on a different CPU without any memory barriers, which
	 * would -- given sufficient re-ordering of memory accesses -- result
	 * in it reading pre-initialization values from the structure.
	 */
	if ((rc = pthread_mutex_init(&ctl->mtx, NULL)) != 0) {
		warn0("pthread_mutex_init: %s", strerror(rc));
		goto err1;
	}
	if ((rc = pthread_mutex_lock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_lock: %s", strerror(rc));
		goto err2;
	}

	/* Create has-work condition variable. */
	if ((rc = pthread_cond_init(&ctl->cv, NULL)) != 0) {
		warn0("pthread_cond_init: %s", strerror(rc));
		goto err3;
	}

	/* No work to do, no work finished yet, no need to suicide. */
	ctl->haswork = ctl->workdone = ctl->suicide = 0;

	/* Static state data. */
	ctl->ID = ID;
	ctl->sstate = sstate;
	ctl->wakeupsock = wakeupsock;

	/* Create the thread. */
	if ((rc = pthread_create(&ctl->thr, NULL, workthread, ctl)) != 0) {
		warn0("pthread_create: %s", strerror(rc));
		goto err4;
	}

	/* Unlock the mutex. */
	if ((rc = pthread_mutex_unlock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_unlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (ctl);

err4:
	pthread_cond_destroy(&ctl->cv);
err3:
	pthread_mutex_unlock(&ctl->mtx);
err2:
	pthread_mutex_destroy(&ctl->mtx);
err1:
	free(ctl);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * worker_assign(ctl, op, blkno, nblks, buf, reqID):
 * Assign the work tuple (${op}, ${blkno}, ${nblks}, ${buf}, ${reqID}) to the
 * thread with work control structure ${ctl} and wake it up.
 */
int
worker_assign(struct workctl * ctl, int op, uint64_t blkno, size_t nblks,
    uint8_t * buf, uint64_t reqID)
{
	int rc;

	/* Lock the control structure. */
	if ((rc = pthread_mutex_lock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_lock: %s", strerror(rc));
		goto err0;
	}

	/* Sanity check: The thread shouldn't be busy. */
	assert(ctl->haswork == 0);

	/* Record the work to be done. */
	ctl->op = op;
	ctl->blkno = blkno;
	ctl->nblks = nblks;
	ctl->buf = buf;
	ctl->reqID = reqID;

	/* The work isn't finished yet. */
	ctl->workdone = 0;

	/* Wake up the thread and unlock the control structure. */
	ctl->haswork = 1;
	if ((rc = pthread_cond_signal(&ctl->cv)) != 0) {
		warn0("pthread_cond_signal: %s", strerror(rc));
		goto err1;
	}
	if ((rc = pthread_mutex_unlock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_unlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	pthread_mutex_unlock(&ctl->mtx);
err0:
	/* Failure! */
	return (-1);
}

/**
 * worker_getdone(ctl, op, blkno, nblks, buf, reqID):
 * Set (${op}, ${blkno}, ${nblks}, ${buf}, ${reqID}) to the work tuple which
 * has been completed by the thread with work control structure ${ctl}, and
 * mark the thread as having no work.
 */
int
worker_getdone(struct workctl * ctl, int * op, uint64_t * blkno,
    size_t * nblks, uint8_t ** buf, uint64_t * reqID)
{
	int rc;

	/* Lock the control structure. */
	if ((rc = pthread_mutex_lock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_lock: %s", strerror(rc));
		goto err0;
	}

	/* Sanity check: This thread should have finished some work. */
	assert(ctl->haswork != 0);
	assert(ctl->workdone != 0);

	/* Copy out the work tuple. */
	*op = ctl->op;
	*blkno = ctl->blkno;
	*nblks = ctl->nblks;
	*buf = ctl->buf;
	*reqID = ctl->reqID;

	/* This thread no longer has work assigned to it. */
	ctl->haswork = 0;

	/* Unlock the thread control structure. */
	if ((rc = pthread_mutex_unlock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_unlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * worker_kill(ctl):
 * Tell the thread with control structure ${ctl} to die and clean it up.
 */
int
worker_kill(struct workctl * ctl)
{
	int rc;

	/* Lock the control structure. */
	if ((rc = pthread_mutex_lock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_lock: %s", strerror(rc));
		goto err0;
	}

	/* Tell the thread to die, and wake it up. */
	ctl->suicide = 1;
	if ((rc = pthread_cond_signal(&ctl->cv)) != 0) {
		warn0("pthread_cond_signal: %s", strerror(rc));
		goto err1;
	}

	/* Unlock the control structure. */
	if ((rc = pthread_mutex_unlock(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_unlock: %s", strerror(rc));
		goto err0;
	}

	/* Wait for the thread to die. */
	if ((rc = pthread_join(ctl->thr, NULL)) != 0) {
		warn0("pthread_join: %s", strerror(rc));
		goto err0;
	}

	/* Destroy condition variable. */
	if ((rc = pthread_cond_destroy(&ctl->cv)) != 0) {
		warn0("pthread_cond_destroy: %s", strerror(rc));
		goto err0;
	}

	/* Destroy mutex. */
	if ((rc = pthread_mutex_destroy(&ctl->mtx)) != 0) {
		warn0("pthread_mutex_destroy: %s", strerror(rc));
		goto err0;
	}

	/* Free the control structure. */
	free(ctl);

	/* Success! */
	return (0);

err1:
	pthread_mutex_unlock(&ctl->mtx);
err0:
	/* Failure! */
	return (-1);
}

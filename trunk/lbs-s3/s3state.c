#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "proto_lbs.h"
#include "proto_s3.h"
#include "warnp.h"
#include "wire.h"

#include "deleteto.h"
#include "findlast.h"
#include "objmap.h"

#include "s3state.h"

#define BLKSPEROBJECT (1 << 24)
#define BLK2OBJECT(blk) ((blk) / BLKSPEROBJECT + 1)
#define BLKOFFSET(blk, blklen) (((blk) % BLKSPEROBJECT) * (blklen))

static int callback_putdone(void *, int);
static int callback_get(void *, int, size_t, const uint8_t *);
static int callback_append(void *, int);

struct get_cookie {
	struct s3state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *,
	    const uint8_t *, size_t);
	void * cookie;
};

struct append_cookie {
	struct s3state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *, uint64_t);
	void * cookie;
};

/**
 * s3state_init(Q_S3, bucket, blklen, D):
 * Initialize the S3 state for bucket ${bucket} containing blocks of length
 * ${blklen} bytes by sending S3 requests via the request queue ${Q_S3}.  Use
 * the DeleteTo state ${D} for handling garbage collection requests.  Return a
 * state which can be passed to other s3state_* functions.  This function may
 * call events_run internally.
 */
struct s3state *
s3state_init(struct wire_requestqueue * Q_S3, const char * bucket,
    size_t blklen, struct deleteto * D)
{
	struct s3state * S;
	uint64_t L;
	size_t olen;
	int putdone;

	/* Allocate a structure and initialize. */
	if ((S = malloc(sizeof(struct s3state))) == NULL)
		goto err0;
	S->Q_S3 = Q_S3;
	S->D = D;
	S->blklen = blklen;
	S->npending = 0;

	/* Duplicate bucket name string. */
	if ((S->bucket = strdup(bucket)) == NULL)
		goto err1;

	/* Find the last written (non-empty) object and its size. */
	if (findlast(Q_S3, bucket, &L, &olen))
		goto err2;

	/* If we have no objects stored, just set initial values. */
	if (L == 0) {
		S->lastblk = (uint64_t)(-1);
		S->nextblk = 0;
		goto done;
	}

	/* The object size should be a multiple of the block size. */
	if (olen % blklen != 0) {
		warn0("S3 object size is not a multiple of the block size!");
		goto err2;
	}

	/* Compute the last block #. */
	S->lastblk = (L - 1) * BLKSPEROBJECT + olen / blklen - 1;

	/*
	 * Write an empty object #(L+1): We can't use this object number for
	 * data because a previous LBS-S3 might have issued a PUT for it
	 * before dying, but we need to ensure that the object exists in order
	 * for the FindLast algorithm to work.
	 */
	putdone = 0;
	if (proto_s3_request_put(S->Q_S3, S->bucket,
	    objmap(L + 1), 0, NULL, callback_putdone, &putdone))
		goto err2;
	if (events_spin(&putdone))
		goto err2;

	/* The next block # is the start of object #(L+2). */
	S->nextblk = (L + 1) * BLKSPEROBJECT;

done:
	/* Success! */
	return (S);

err2:
	free(S->bucket);
err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/* Callback for empty PUT from initialization. */
static int
callback_putdone(void * cookie, int failed)
{
	int * putdone = cookie;

	/* We're done. */
	*putdone = 1;

	/* A failure here is a fatal. */
	return (failed);
}

/**
 * s3state_get(S, R, callback, cookie):
 * Perform the GET operation specified by the LBS protocol request ${R} on the
 * S3 state ${S}.  Invoke ${callback}(${cookie}, ${R}, buf, blklen) when done,
 * where ${blklen} is the block size and ${buf} contains the requested block
 * data or is NULL if the block does not exist.
 */
int
s3state_get(struct s3state * S, struct proto_lbs_request * R,
    int (* callback)(void *, struct proto_lbs_request *,
        const uint8_t *, size_t), void * cookie)
{
	struct get_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct get_cookie))) == NULL)
		goto err0;
	C->S = S;
	C->R = R;
	C->callback = callback;
	C->cookie = cookie;

	/* Send the S3 request. */
	if (proto_s3_request_range(S->Q_S3, S->bucket, 
	    objmap(BLK2OBJECT(R->r.get.blkno)),
	    BLKOFFSET(R->r.get.blkno, S->blklen), S->blklen,
	    callback_get, C))
		goto err1;

	/* We will be performing a callback later. */
	S->npending += 1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* Callback for S3 RANGEs performed by s3state_get. */
static int
callback_get(void * cookie, int failed, size_t buflen, const uint8_t * buf)
{
	struct get_cookie * C = cookie;
	struct s3state * S = C->S;
	int rc;

	/* If we failed or have the wrong size of buffer, we have no block. */
	if ((failed != 0) || (buflen != S->blklen))
		buf = NULL;

	/* Tell the dispatcher to send its response back. */
	rc = (C->callback)(C->cookie, C->R, buf, buflen);

	/* We've done a callback. */
	S->npending -= 1;

	/* Free our cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * s3state_append(S, R, callback, cookie):
 * Perform the APPEND operation specified by the LBS protocol request ${R} on
 * the S3 state ${S}.  Invoke ${callback}(${cookie}, ${R}, nextblk) when done,
 * where ${nextblk} is the next available block # after this append.
 */
int 
s3state_append(struct s3state * S, struct proto_lbs_request * R,
    int (* callback)(void *, struct proto_lbs_request *, uint64_t),
    void * cookie)
{
	struct append_cookie * C;

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct append_cookie))) == NULL)
		goto err0;
	C->S = S;
	C->R = R;
	C->callback = callback;
	C->cookie = cookie;

	/* Sanity check. */
	assert(R->r.append.blkno % BLKSPEROBJECT == 0);

	/* Send the S3 request. */
	if (proto_s3_request_put(S->Q_S3, S->bucket,
	    objmap(BLK2OBJECT(R->r.append.blkno)),
	    R->r.append.nblks * S->blklen, R->r.append.buf,
	    callback_append, C))
		goto err1;

	/* We will be performing a callback later. */
	S->npending += 1;

	/* Success! */
	return (0);

err1:
	free(C);
err0:
	/* Failure! */
	return (-1);
}

/* Callback for S3 PUTs performed by s3state_append. */
static int
callback_append(void * cookie, int failed)
{
	struct append_cookie * C = cookie;
	struct s3state * S = C->S;
	int rc;

	/* If S3 isn't talking to us, we're screwed. */
	if (failed)
		goto err1;

	/* Update the last-block and next-block values based on this append. */
	S->nextblk = C->R->r.append.blkno + BLKSPEROBJECT;
	S->lastblk = C->R->r.append.blkno + C->R->r.append.nblks - 1;

	/* Tell the dispatcher to send its response back. */
	rc = (C->callback)(C->cookie, C->R, S->nextblk);

	/* We've done a callback. */
	S->npending -= 1;

	/* Free our cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);

err1:
	free(C);

	/* Failure! */
	return (-1);
}

/**
 * s3state_gc(S, blkno):
 * Garbage collect (or mark as available for garbage collection) all blocks
 * less than ${blkno} in the S3 state ${S}.
 */
int
s3state_gc(struct s3state * S, uint64_t blkno)
{

	/* We can delete objects below the one which blkno belongs to. */
	return (deleteto_deleteto(S->D, BLK2OBJECT(blkno)));
}

/**
 * s3state_free(S):
 * Free the S3 state ${S}.  This function must only be called when there are
 * no s3state_get or s3state_append callbacks pending.
 */
void
s3state_free(struct s3state * S)
{

	/* Sanity-check. */
	assert(S->npending == 0);

	/* Free allocations. */
	free(S->bucket);
	free(S);
}

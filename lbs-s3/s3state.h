#ifndef _S3STATE_H_
#define _S3STATE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct deleteto;
struct proto_lbs_request;
struct wire_requestqueue;

/* S3 state structure. */
struct s3state {
	/* Bits dispatch.c needs to look at. */
	uint32_t blklen;	/* Block size. */
	uint64_t nextblk;	/* Next available block #. */
	uint64_t lastblk;	/* Last written block #. */

	/* Internal data. */
	struct wire_requestqueue * Q_S3;	/* Connected to S3 daemon. */
	struct deleteto * D;	/* DeleteTo state. */
	char * bucket;		/* Bucket name. */
	size_t npending;	/* Callbacks not performed yet. */
};

/**
 * s3state_init(Q_S3, bucket, blklen, D):
 * Initialize the S3 state for bucket ${bucket} containing blocks of length
 * ${blklen} bytes by sending S3 requests via the request queue ${Q_S3}.  Use
 * the DeleteTo state ${D} for handling garbage collection requests.  Return a
 * state which can be passed to other s3state_* functions.  This function may
 * call events_run() internally.
 */
struct s3state * s3state_init(struct wire_requestqueue *, const char *,
    size_t, struct deleteto *);

/**
 * s3state_get(S, R, callback, cookie):
 * Perform the GET operation specified by the LBS protocol request ${R} on the
 * S3 state ${S}.  Invoke ${callback}(${cookie}, ${R}, buf, blklen) when done,
 * where ${blklen} is the block size and ${buf} contains the requested block
 * data or is NULL if the block does not exist.
 */
int s3state_get(struct s3state *, struct proto_lbs_request *,
    int (*)(void *, struct proto_lbs_request *, const uint8_t *, size_t),
    void *);

/**
 * s3state_append(S, R, callback, cookie):
 * Perform the APPEND operation specified by the LBS protocol request ${R} on
 * the S3 state ${S}.  Invoke ${callback}(${cookie}, ${R}, nextblk) when done,
 * where ${nextblk} is the next available block # after this append.
 */
int s3state_append(struct s3state *, struct proto_lbs_request *,
    int (*)(void *, struct proto_lbs_request *, uint64_t), void *);

/**
 * s3state_gc(S, blkno):
 * Garbage collect (or mark as available for garbage collection) all blocks
 * less than ${blkno} in the S3 state ${S}.
 */
int s3state_gc(struct s3state *, uint64_t);

/**
 * s3state_free(S):
 * Free the S3 state ${S}.  This function must only be called when there are
 * no s3state_get() or s3state_append() callbacks pending.
 */
void s3state_free(struct s3state *);

#endif /* !_S3STATE_H_ */

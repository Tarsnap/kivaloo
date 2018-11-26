#ifndef _STATE_H_
#define _STATE_H_

/* Opaque types. */
struct deleteto;
struct proto_lbs_request;
struct wire_requestqueue;

/* Internal state structure. */
struct state {
	/* Bits dispatch.c needs to look at. */
	uint32_t blklen;	/* Block size. */
	uint64_t lastblk;	/* Last written block #. */

	/* Internal data. */
	struct wire_requestqueue * Q;	/* Connected to DDBKV daemon. */
	struct deleteto * D;	/* DeleteTo state. */
	size_t npending;	/* Callbacks not performed yet. */
};

/**
 * state_init(Q_DDBKV, itemsz, D):
 * Initialize the internal state for handling DynamoDB items of ${itemsz}
 * bytes, using the DynamoDB-KV daemon connected to ${Q_DDBKV}.  Use the
 * DeleteTo state ${D} for handling garbage collection requests.  Return a
 * state which can be passed to other state_* functions.  This function may
 * call events_run internally.
 */
struct state * state_init(struct wire_requestqueue *, size_t,
    struct deleteto *);

/**
 * state_get(S, R, callback, cookie):
 * Perform the GET operation specified by the LBS protocol request ${R} on the
 * state ${S}.  Invoke ${callback}(${cookie}, ${R}, buf, blklen) when done,
 * where ${blklen} is the block size and ${buf} contains the requested block
 * data or is NULL if the block does not exist.
 */
int state_get(struct state *, struct proto_lbs_request *,
    int (*)(void *, struct proto_lbs_request *, const uint8_t *, size_t),
    void *);

/**
 * state_append(S, R, callback, cookie):
 * Perform the APPEND operation specified by the LBS protocol request ${R} on
 * the state ${S}.  Invoke ${callback}(${cookie}, ${R}) when done.
 */
int state_append(struct state *, struct proto_lbs_request *,
    int (*)(void *, struct proto_lbs_request *), void *);

/**
 * state_gc(S, blkno):
 * Garbage collect (or mark as available for garbage collection) all blocks
 * less than ${blkno} in the state ${S}.
 */
int state_gc(struct state *, uint64_t);

/**
 * state_free(S):
 * Free the internal state ${S}.  This function must only be called when
 * there are no state_get or state_append callbacks pending.
 */
void state_free(struct state *);

#endif /* !_STATE_H_ */

#ifndef _STATE_H_
#define _STATE_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct metadata;
struct proto_lbs_request;
struct wire_requestqueue;

/**
 * state_init(Q_DDBKV, itemsz, tableid, M):
 * Initialize the internal state for handling DynamoDB items of ${itemsz}
 * bytes, using the DynamoDB-KV daemon connected to ${Q_DDBKV}.  Verify that
 * the (data) table matches the provided table ID.  Use the metadata handler
 * ${M} to handle metadata.  Return a state which can be passed to other
 * state_* functions.  This function may call events_run() internally.
 */
struct state * state_init(struct wire_requestqueue *, size_t, uint8_t *,
    struct metadata *);

/**
 * state_params(S, blklen, lastblk, nextblk):
 * Return the block size, the last stored block #, and next block # to write
 * via the provided pointers.
 */
void state_params(struct state *, uint32_t *, uint64_t *, uint64_t *);

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
 * the state ${S}.  Invoke ${callback}(${cookie}, ${R}, nextblk) when done.
 */
int state_append(struct state *, struct proto_lbs_request *,
    int (*)(void *, struct proto_lbs_request *, uint64_t), void *);

/**
 * state_free(S):
 * Free the internal state ${S}.  This function must only be called when
 * there are no state_get() or state_append() callbacks pending.
 */
void state_free(struct state *);

#endif /* !_STATE_H_ */

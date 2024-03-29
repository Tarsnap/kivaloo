#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "proto_dynamodb_kv.h"
#include "proto_lbs.h"
#include "warnp.h"
#include "wire.h"

#include "metadata.h"
#include "objmap.h"

#include "state.h"

/* Internal state structure. */
struct state {
	uint32_t blklen;	/* Block size. */
	uint64_t lastblk;	/* Last block # written. */
	uint64_t nextblk;	/* Next block # to write. */
	struct wire_requestqueue * Q;	/* Connected to DDBKV daemon. */
	struct metadata * M;	/* Metadata handler. */
	size_t npending;	/* Callbacks not performed yet. */
};

struct readtableid {
	int done;
	uint8_t tableid[32];
};

static int callback_get(void *, int, const uint8_t *, size_t);

struct get_cookie {
	struct state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *,
	    const uint8_t *, size_t);
	void * cookie;
	int consistent;
};

int callback_append_put_nextblk(void *);
int callback_append_put_blks(void *, int);
int callback_append_put_lastblk(void *);

struct append_cookie {
	struct state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *, uint64_t);
	void * cookie;
	uint64_t nblks_left;
	uint64_t nextblk_old;
};

/* Overhead per KV item: Item size minus block size. */
#define KVOVERHEAD 18

/* Callback for reading tableid. */
static int
callback_init(void * cookie, int status, const uint8_t * buf, size_t buflen)
{
	struct readtableid * RT = cookie;

	/* Sanity-check. */
	assert((status == 0) || (status == 1) || (status == 2));

	/* Check status. */
	if (status == 1) {
		warn0("Failed to read tableid");
		goto err0;
	} else if (status == 2) {
		warn0("Tableid not initialized");
		goto err0;
	}

	/* Check buffer length. */
	if (buflen != 32) {
		warn0("Tableid is not 32 bytes");
		goto err0;
	}

	/* Copy the table ID out. */
	memcpy(RT->tableid, buf, 32);

	/* We've finished reading this value. */
	RT->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * state_init(Q_DDBKV, itemsz, tableid, M):
 * Initialize the internal state for handling DynamoDB items of ${itemsz}
 * bytes, using the DynamoDB-KV daemon connected to ${Q_DDBKV}.  Verify that
 * the (data) table matches the provided table ID.  Use the metadata handler
 * ${M} to handle metadata.  Return a state which can be passed to other
 * state_* functions.  This function may call events_run() internally.
 */
struct state *
state_init(struct wire_requestqueue * Q_DDBKV, size_t itemsz, uint8_t * tableid,
    struct metadata * M)
{
	struct state * S;
	struct readtableid RT;

	/* Sanity check. */
	assert(itemsz - KVOVERHEAD <= UINT32_MAX);

	/* Allocate a structure and initialize. */
	if ((S = malloc(sizeof(struct state))) == NULL)
		goto err0;
	S->Q = Q_DDBKV;
	S->M = M;
	S->blklen = (uint32_t)(itemsz - KVOVERHEAD);
	S->npending = 0;

	/* Read "nextblk" and "lastblk" values. */
	S->nextblk = metadata_nextblk_read(S->M);
	S->lastblk = metadata_lastblk_read(S->M);

	/* Read tableid from the table. */
	RT.done = 0;
	if (proto_dynamodb_kv_request_getc(S->Q, "tableid", callback_init, &RT))
		goto err1;
	if (events_spin(&RT.done)) {
		warnp("Error reading tableid");
		goto err1;
	}

	/* Verify that the table IDs match. */
	if (memcmp(RT.tableid, tableid, 32)) {
		warn0("Data table ID does not match metadata table ID!");
		goto err1;
	}

	/* Success! */
	return (S);

err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * state_params(S, blklen, lastblk, nextblk):
 * Return the block size, the last stored block #, and next block # to write
 * via the provided pointers.
 */
void
state_params(struct state * S, uint32_t * blklen, uint64_t * lastblk,
    uint64_t * nextblk)
{

	/* Extract the requested fields from our internal state. */
	*blklen = S->blklen;
	*lastblk = S->lastblk;
	*nextblk = S->nextblk;
}

/**
 * state_get(S, R, callback, cookie):
 * Perform the GET operation specified by the LBS protocol request ${R} on the
 * state ${S}.  Invoke ${callback}(${cookie}, ${R}, buf, blklen) when done,
 * where ${blklen} is the block size and ${buf} contains the requested block
 * data or is NULL if the block does not exist.
 */
int
state_get(struct state * S, struct proto_lbs_request * R,
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
	C->consistent = 0;

	/* Send the request. */
	if (proto_dynamodb_kv_request_get(S->Q, objmap(R->r.get.blkno),
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

/* Callback for GET requests. */
static int
callback_get(void * cookie, int status, const uint8_t * buf, size_t buflen)
{
	struct get_cookie * C = cookie;
	struct proto_lbs_request * R = C->R;
	struct state * S = C->S;
	int rc;

	/* Sanity-check. */
	assert((status == 0) || (status == 1) || (status == 2));

	/*
	 * If an eventually-consistent read didn't find the block, try again
	 * with a strong-consistency read.
	 */
	if ((status == 2) && (C->consistent == 0)) {
		C->consistent = 1;
		return (proto_dynamodb_kv_request_getc(S->Q,
		    objmap(R->r.get.blkno), callback_get, C));
	}

	/* If we got data, verify the block size. */
	if ((status == 0) && (buflen != S->blklen)) {
		warn0("DynamoDB-KV GET returned wrong amount of data:"
		    " %zu (should be %" PRIu32 ")", buflen, S->blklen);
		goto err0;
	}

	/* Failures are bad. */
	if (status == 1) {
		warnp("Failure in DynamoDB-KV GET");
		goto err0;
	}

	/* If the block does not exist, we have no data. */
	if (status == 2)
		buf = NULL;

	/* Tell the dispatcher to send its response back. */
	rc = (C->callback)(C->cookie, C->R, buf, S->blklen);

	/* We've done a callback. */
	S->npending -= 1;

	/* Free our cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);

err0:
	/* Failure! */
	return (-1);
}

/**
 * state_append(S, R, callback, cookie):
 * Perform the APPEND operation specified by the LBS protocol request ${R} on
 * the state ${S}.  Invoke ${callback}(${cookie}, ${R}, nextblk) when done.
 */
int
state_append(struct state * S, struct proto_lbs_request * R,
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
	C->nblks_left = R->r.append.nblks;
	C->nextblk_old = S->nextblk;

	/* Update nextblk. */
	S->nextblk += R->r.append.nblks;
	if (metadata_nextblk_write(S->M, S->nextblk,
	    callback_append_put_nextblk, C))
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

/* Callback when "nextblk" has been written. */
int
callback_append_put_nextblk(void * cookie)
{
	struct append_cookie * C = cookie;
	struct state * S = C->S;
	struct proto_lbs_request * R = C->R;
	size_t i;

	/* Store all the blocks. */
	for (i = 0; i < R->r.append.nblks; i++) {
		if (proto_dynamodb_kv_request_put(S->Q,
		    objmap(C->nextblk_old + i),
		    &R->r.append.buf[i * S->blklen], S->blklen,
		    callback_append_put_blks, C))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback when a block, other than the final block, has been written. */
int
callback_append_put_blks(void * cookie, int status)
{
	struct append_cookie * C = cookie;
	struct state * S = C->S;

	/* Sanity-check: We should be storing blocks. */
	assert(C->nblks_left != 0);

	/* Failures are bad. */
	if (status) {
		warn0("DynamoDB-KV failed storing data block");
		goto err0;
	}

	/* We've stored a block. */
	C->nblks_left--;

	/* If we've stored all the blocks, record a new lastblk value. */
	if (C->nblks_left == 0) {
		S->lastblk = S->nextblk - 1;
		if (metadata_lastblk_write(S->M, S->lastblk,
		    callback_append_put_lastblk, C))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback when a new lastblk value has been stored. */
int
callback_append_put_lastblk(void * cookie)
{
	struct append_cookie * C = cookie;
	struct state * S = C->S;
	int rc;

	/* Tell the dispatcher to send its response back. */
	rc = (C->callback)(C->cookie, C->R, S->nextblk);

	/* We've done a callback. */
	S->npending -= 1;

	/* Free our cookie. */
	free(C);

	/* Return status from callback. */
	return (rc);
}

/**
 * state_free(S):
 * Free the internal state ${S}.  This function must only be called when
 * there are no state_get() or state_append() callbacks pending.
 */
void
state_free(struct state * S)
{

	/* Sanity-check. */
	assert(S->npending == 0);

	/* Free allocation. */
	free(S);
}

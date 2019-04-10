#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "metadata.h"
#include "proto_lbs.h"
#include "proto_dynamodb_kv.h"
#include "warnp.h"
#include "wire.h"

#include "deleteto.h"
#include "objmap.h"

#include "state.h"

static int callback_get(void *, int, const uint8_t *, size_t);

struct get_cookie {
	struct state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *,
	    const uint8_t *, size_t);
	void * cookie;
	int consistent;
};

int callback_append_put_lastblk(void *, int);
int callback_append_put_blks(void *, int);
int callback_append_put_finalblk(void *, int);

struct append_cookie {
	struct state * S;
	struct proto_lbs_request * R;
	int (* callback)(void *, struct proto_lbs_request *);
	void * cookie;
	uint64_t nblks_left;
	uint64_t lastblk_new;
};

/* Overhead per KV item: Item size minus block size. */
#define KVOVERHEAD 18

/**
 * state_init(Q_DDBKV, itemsz, D):
 * Initialize the internal state for handling DynamoDB items of ${itemsz}
 * bytes, using the DynamoDB-KV daemon connected to ${Q_DDBKV}.  Use the
 * DeleteTo state ${D} for handling garbage collection requests.  Return a
 * state which can be passed to other state_* functions.  This function may
 * call events_run internally.
 */
struct state *
state_init(struct wire_requestqueue * Q_DDBKV,
    size_t itemsz, struct deleteto * D)
{
	struct state * S;

	/* Allocate a structure and initialize. */
	if ((S = malloc(sizeof(struct state))) == NULL)
		goto err0;
	S->Q = Q_DDBKV;
	S->D = D;
	S->blklen = itemsz - KVOVERHEAD;
	S->npending = 0;

	/* Read "lastblk"; we *might* have written up to here. */
	if (metadata_lastblk_read(S->Q, &S->lastblk))
		goto err1;

	/* Success! */
	return (S);

err1:
	free(S);
err0:
	/* Failure! */
	return (NULL);
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
		    " %zu (should be %zu)", buflen, S->blklen);
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
 * the state ${S}.  Invoke ${callback}(${cookie}, ${R}) when done.
 */
int
state_append(struct state * S, struct proto_lbs_request * R,
    int (* callback)(void *, struct proto_lbs_request *), void * cookie)
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
	C->lastblk_new = S->lastblk + R->r.append.nblks;

	/* Store the new value of lastblk. */
	if (metadata_lastblk_write(S->Q, C->lastblk_new,
	    callback_append_put_lastblk, C))
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

/* Callback when "lastblk" has been written. */
int
callback_append_put_lastblk(void * cookie, int status)
{
	struct append_cookie * C = cookie;
	struct state * S = C->S;
	struct proto_lbs_request * R = C->R;
	size_t i;

	/* Failures are bad. */
	if (status) {
		warn0("DynamoDB-KV failed storing \"lastblk\"");
		goto err0;
	}

	/* Store all the blocks except the last one. */
	for (i = 0; i + 1 < R->r.append.nblks; i++) {
		if (proto_dynamodb_kv_request_put(S->Q,
		    objmap(S->lastblk + 1 + i),
		    &R->r.append.buf[i * S->blklen], S->blklen,
		    callback_append_put_blks, C))
			goto err0;
	}

	/* If there only was one block, it. */
	if (R->r.append.nblks == 1) {
		i = R->r.append.nblks - 1;
		if (proto_dynamodb_kv_request_put(S->Q,
		    objmap(S->lastblk + 1 + i),
		    &R->r.append.buf[i * S->blklen], S->blklen,
		    callback_append_put_finalblk, C))
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
	struct proto_lbs_request * R = C->R;
	size_t i;

	/* Failures are bad. */
	if (status) {
		warn0("DynamoDB-KV failed storing \"lastblk\"");
		goto err0;
	}

	/* We've stored a block. */
	C->nblks_left--;

	/*
	 * If there's only one block left to store, it's the final block in
	 * the request; store it now.
	 */

	if (C->nblks_left == 1) {
		i = R->r.append.nblks - 1;
		if (proto_dynamodb_kv_request_put(S->Q,
		    objmap(S->lastblk + 1 + i),
		    &R->r.append.buf[i * S->blklen], S->blklen,
		    callback_append_put_finalblk, C))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback when the final block has been written. */
int
callback_append_put_finalblk(void * cookie, int status)
{
	struct append_cookie * C = cookie;
	struct state * S = C->S;
	int rc;

	/* This had better have been the only block left to store. */
	assert(C->nblks_left == 1);

	/* Failures are bad. */
	if (status) {
		warn0("DynamoDB-KV failed storing \"lastblk\"");
		goto err1;
	}

	/* Update the last-block value. */
	S->lastblk = C->lastblk_new;

	/* Tell the dispatcher to send its response back. */
	rc = (C->callback)(C->cookie, C->R);

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
 * state_gc(S, blkno):
 * Garbage collect (or mark as available for garbage collection) all blocks
 * less than ${blkno} in the state ${S}.
 */
int
state_gc(struct state * S, uint64_t blkno)
{

	/* Pass this along to the deleter. */
	return (deleteto_deleteto(S->D, blkno));
}

/**
 * state_free(S):
 * Free the internal state ${S}.  This function must only be called when
 * there are no state_get or state_append callbacks pending.
 */
void
state_free(struct state * S)
{

	/* Sanity-check. */
	assert(S->npending == 0);

	/* Free allocation. */
	free(S);
}

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "proto_dynamodb_kv.h"
#include "sysendian.h"
#include "warnp.h"

#include "metadata.h"

/* Metadata tuple. */
struct mtuple {
	uint64_t nextblk;
	uint64_t deletedto;
	uint64_t generation;
};

/* Internal state. */
struct metadata {
	struct wire_requestqueue * Q;
	struct mtuple M_stored;
	struct mtuple M_storing;
	struct mtuple M_latest;
	int (* callback_nextblk)(void *, int);
	void * cookie_nextblk;
	int (* callback_deletedto)(void *, int);
	void * cookie_deletedto;
	int callbacks;
#define CB_NEXTBLK 1
#define CB_DELETEDTO 2
	void * timer_cookie;
};

static int callback_readmetadata(void *, int, const uint8_t *, size_t);
static int callback_writemetadata(void *, int);

/* Used for reading metadata during initialization. */
struct readmetadata {
	uint64_t nextblk;
	uint64_t deletedto;
	uint64_t generation;
	int done;
};

static int
readmetadata(struct wire_requestqueue * Q, struct readmetadata * R)
{

	/* Read the metadata. */
	R->done = 0;
	if (proto_dynamodb_kv_request_getc(Q, "metadata",
	    callback_readmetadata, R) ||
	    events_spin(&R->done)) {
		warnp("Error reading LBS metadata");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_readmetadata(void * cookie, int status,
    const uint8_t * buf, size_t len)
{
	struct readmetadata * R = cookie;

	/* Failures are bad. */
	if (status == 1)
		goto err0;

	/* Did the item exist? */
	if (status == 2) {
		/*
		 * If we have no metadata, we have no data: The next block
		 * is block 0, and everything below block 0 has been deleted.
		 */
		R->nextblk = 0;
		R->deletedto = 0;
		R->generation = 0;
		goto done;
	}

	/* We should have 24 bytes. */
	if (len != 24) {
		warn0("metadata has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	R->nextblk = be64dec(&buf[0]);
	R->deletedto = be64dec(&buf[8]);
	R->generation = be64dec(&buf[16]);

done:
	R->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
writemetadata(struct metadata * M)
{
	uint8_t buf[24];

	/* If we have a timer in progress, cancel it. */
	if (M->timer_cookie != NULL) {
		events_timer_cancel(M->timer_cookie);
		M->timer_cookie = NULL;
	}

	/* We're going to store the latest metadata values. */
	M->M_storing = M->M_latest;

	/* Increment metadata generation for the next metadata stored. */
	M->M_latest.generation++;

	/* Encode metadata. */
	be64enc(&buf[0], M->M_storing.nextblk);
	be64enc(&buf[8], M->M_storing.deletedto);
	be64enc(&buf[16], M->M_storing.generation);

	/* Record which callbacks to perform later. */
	if (M->callback_nextblk)
		M->callbacks += CB_NEXTBLK;
	if (M->callback_deletedto)
		M->callbacks += CB_DELETEDTO;

	/* Write metadata. */
	if (proto_dynamodb_kv_request_put(M->Q, "metadata", buf, 24,
	    callback_writemetadata, M))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_writemetadata(void * _M, int status)
{
	struct metadata * M = _M;
	int (* callback)(void *, int);
	void * cookie;
	int rc = 0;
	int rc2;

	/* The values we were storing have now been stored. */
	M->M_stored = M->M_storing;

	/* Perform callbacks as appropriate. */
	if (M->callbacks & CB_NEXTBLK) {
		callback = M->callback_nextblk;
		cookie = M->cookie_nextblk;
		M->callback_nextblk = NULL;
		M->cookie_nextblk = NULL;
		if ((rc2 = (callback)(cookie, status)) != 0)
			rc = rc2;
	}
	if (M->callbacks & CB_DELETEDTO) {
		callback = M->callback_deletedto;
		cookie = M->cookie_deletedto;
		M->callback_deletedto = NULL;
		M->cookie_deletedto = NULL;
		if ((rc2 = (callback)(cookie, status)) != 0)
			rc = rc2;
	}

	/* All callbacks have been performed. */
	M->callbacks = 0;

	/*
	 * Start another write if we have a new value of nextblk, or if we
	 * have a new value of deletedto and the timer has expired.
	 */
	if ((M->callback_nextblk != NULL) ||
	    ((M->callback_deletedto != NULL) && (M->timer_cookie == NULL))) {
		if ((rc2 = writemetadata(M)) != 0)
			rc = rc2;
	}

	/* Return status from callbacks or writemetadata. */
	return (rc);
}

static int
callback_timer(void * cookie)
{
	struct metadata * M = cookie;

	/* This callback is no longer pending. */
	M->timer_cookie = NULL;

	/* Write metadata if a write is not already in progress. */
	if (M->callbacks == 0) {
		if (writemetadata(M))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_init(Q):
 * Prepare for metadata operations using the queue ${Q}.  This function may
 * call events_run() internally.
 */
struct metadata *
metadata_init(struct wire_requestqueue * Q)
{
	struct metadata * M;
	struct readmetadata R;

	/* Bake a cookie. */
	if ((M = malloc(sizeof(struct metadata))) == NULL)
		goto err0;
	M->Q = Q;

	/* Read metadata. */
	if (readmetadata(Q, &R))
		goto err1;
	M->M_stored.nextblk = R.nextblk;
	M->M_stored.deletedto = R.deletedto;
	M->M_stored.generation = R.generation;

	/* The next metadata will be the same except one higher generation. */
	M->M_latest = M->M_stored;
	M->M_latest.generation++;

	/* Nothing in progress yet. */
	M->callback_nextblk = NULL;
	M->cookie_nextblk = NULL;
	M->callback_deletedto = NULL;
	M->cookie_deletedto = NULL;
	M->callbacks = 0;
	M->timer_cookie = NULL;

	/* Success! */
	return (M);

err1:
	free(M);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * metadata_nextblk_read(M, nextblk):
 * Read the "nextblk" value.
 */
int
metadata_nextblk_read(struct metadata * M, uint64_t * nextblk)
{

	*nextblk = M->M_stored.nextblk;

	/* Success! */
	return (0);
}

/**
 * metadata_nextblk_write(M, nextblk, callback, cookie):
 * Store "nextblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_nextblk_write(struct metadata * M, uint64_t nextblk,
    int (*callback)(void *, int), void * cookie)
{

	/* We shouldn't be storing nextblk already. */
	assert(M->callback_nextblk == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.nextblk = nextblk;
	M->callback_nextblk = callback;
	M->cookie_nextblk = cookie;

	/* Write metadata if a write is not already in progress. */
	if (M->callbacks == 0) {
		if (writemetadata(M))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_deletedto_read(M, deletedto):
 * Read the "deletedto" value.
 */
int
metadata_deletedto_read(struct metadata * M, uint64_t * deletedto)
{

	*deletedto = M->M_stored.deletedto;

	/* Success! */
	return (0);
}

/**
 * metadata_deletedto_write(M, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_deletedto_write(struct metadata * M, uint64_t deletedto,
    int (*callback)(void *, int), void * cookie)
{

	/* We shouldn't be storing deletedto already. */
	assert(M->callback_deletedto == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.deletedto = deletedto;
	M->callback_deletedto = callback;
	M->cookie_deletedto = cookie;

	/* Write metadata in 1 second if not triggered previously. */
	if ((M->timer_cookie =
	    events_timer_register_double(callback_timer, M, 1.0)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_free(M):
 * Stop metadata operations.
 */
void
metadata_free(struct metadata * M)
{

	/* Behave consistently with free(NULL). */
	if (M == NULL)
		return;

	/* We shouldn't have any updates or callbacks in flight. */
	assert(M->callback_nextblk == NULL);
	assert(M->callback_deletedto == NULL);
	assert(M->timer_cookie == NULL);

	/* Free our structure. */
	free(M);
}

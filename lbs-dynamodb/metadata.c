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
	uint64_t lastblk;
	int (* callback)(void *);
	void * cookie;
};

/* Internal state. */
struct metadata {
	struct wire_requestqueue * Q;
	struct mtuple M_stored;
	struct mtuple M_storing;
	struct mtuple M_latest;
	int (* callback_deletedto)(void *);
	void * cookie_deletedto;
	int write_inprogress;
	int write_wanted;
};

static int callback_readmetadata(void *, int, const uint8_t *, size_t);
static int callback_writemetadata(void *, int);

/* Used for reading metadata during initialization. */
struct readmetadata {
	uint64_t nextblk;
	uint64_t deletedto;
	uint64_t generation;
	uint64_t lastblk;
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
		R->lastblk = (uint64_t)(-1);
		goto done;
	}

	/* We should have 32 bytes. */
	if (len != 32) {
		warn0("metadata has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	R->nextblk = be64dec(&buf[0]);
	R->deletedto = be64dec(&buf[8]);
	R->generation = be64dec(&buf[16]);
	R->lastblk = be64dec(&buf[24]);

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
	uint8_t buf[32];

	/* Is a write already in progress? */
	if (M->write_inprogress == 1)
		return (0);

	/* We're going to start a write now. */
	M->write_inprogress = 1;
	M->write_wanted = 0;

	/* We're going to store the latest metadata values. */
	M->M_storing = M->M_latest;

	/* The requested callback will be performed when the store completes. */
	M->M_latest.callback = NULL;

	/* Increment metadata generation for the next metadata stored. */
	M->M_latest.generation++;

	/* Encode metadata. */
	be64enc(&buf[0], M->M_storing.nextblk);
	be64enc(&buf[8], M->M_storing.deletedto);
	be64enc(&buf[16], M->M_storing.generation);
	be64enc(&buf[24], M->M_storing.lastblk);

	/* Write metadata. */
	if (proto_dynamodb_kv_request_put(M->Q, "metadata", buf, 32,
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
	int (* callback)(void *);
	void * cookie;
	int rc = 0;
	int rc2;

	/* Sanity-check. */
	assert(M->write_inprogress);

	/* If we failed to write metadata, something went very wrong. */
	if (status) {
		warn0("Failed to store metadata to DynamoDB!");
		goto err0;
	}

	/* We're no longer storing metadata. */
	M->write_inprogress = 0;

	/* The values we were storing have now been stored. */
	M->M_stored = M->M_storing;

	/* Perform callbacks as appropriate. */
	if (M->M_stored.callback != NULL) {
		callback = M->M_stored.callback;
		cookie = M->M_stored.cookie;
		if ((rc2 = (callback)(cookie)) != 0)
			rc = rc2;
	}
	if (M->callback_deletedto != NULL) {
		callback = M->callback_deletedto;
		cookie = M->cookie_deletedto;
		if ((rc2 = (callback)(cookie)) != 0)
			rc = rc2;
	}

	/* Start another write if needed. */
	if (M->write_wanted) {
		if ((rc2 = writemetadata(M)) != 0)
			rc = rc2;
	}

	/* Return status from callbacks or writemetadata. */
	return (rc);

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
	M->M_stored.lastblk = R.lastblk;

	/* The next metadata will be the same except one higher generation. */
	M->M_latest = M->M_stored;
	M->M_latest.generation++;

	/* Nothing in progress yet. */
	M->M_latest.callback = NULL;
	M->M_latest.cookie = NULL;
	M->callback_deletedto = NULL;
	M->cookie_deletedto = NULL;
	M->write_inprogress = 0;
	M->write_wanted = 0;

	/* Success! */
	return (M);

err1:
	free(M);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * metadata_nextblk_read(M):
 * Return the "nextblk" value.
 */
uint64_t
metadata_nextblk_read(struct metadata * M)
{

	/* Return currently stored value. */
	return (M->M_stored.nextblk);
}

/**
 * metadata_nextblk_write(M, nextblk, callback, cookie):
 * Store "nextblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_nextblk_write(struct metadata * M, uint64_t nextblk,
    int (*callback)(void *), void * cookie)
{

	/* We shouldn't have a callback already. */
	assert(M->M_latest.callback == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.nextblk = nextblk;
	M->M_latest.callback = callback;
	M->M_latest.cookie = cookie;

	/* We want to write metadata as soon as possible. */
	if (writemetadata(M))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_lastblk_read(M):
 * Return the "lastblk" value.
 */
uint64_t
metadata_lastblk_read(struct metadata * M)
{

	/* Return currently stored value. */
	return (M->M_stored.lastblk);
}

/**
 * metadata_lastblk_write(M, lastblk, callback, cookie):
 * Store "lastblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_lastblk_write(struct metadata * M, uint64_t lastblk,
    int (*callback)(void *), void * cookie)
{

	/* We shouldn't have a callback already. */
	assert(M->M_latest.callback == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.lastblk = lastblk;
	M->M_latest.callback = callback;
	M->M_latest.cookie = cookie;

	/* We want to write metadata as soon as possible. */
	if (writemetadata(M))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_deletedto_read(M):
 * Return the "deletedto" value.
 */
uint64_t
metadata_deletedto_read(struct metadata * M)
{

	/* Return currently stored value. */
	return (M->M_stored.deletedto);
}

/**
 * metadata_deletedto_write(M, deletedto, callback, cookie):
 * Store "deletedto" value.
 */
void
metadata_deletedto_write(struct metadata * M, uint64_t deletedto)
{

	/* Record the new value. */
	M->M_latest.deletedto = deletedto;
}

/**
 * metadata_deletedto_register(M, callback, cookie):
 * Register ${callback}(${cookie}) to be called every time metadata is stored.
 * This API exists for the benefit of the deletedto code; only one callback can
 * be registered in this manner at once, and the callback must be reset to NULL
 * before metadata_free is called.
 */
void
metadata_deletedto_register(struct metadata * M,
    int (*callback)(void *), void * cookie)
{

	/* Record the callback and cookie. */
	M->callback_deletedto = callback;
	M->cookie_deletedto = cookie;
}

/**
 * metadata_flush(M):
 * Trigger a flush of pending metadata updates.
 */
int
metadata_flush(struct metadata * M)
{

	/* We want to write metadata as soon as possible. */
	return (writemetadata(M));
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
	assert(M->M_latest.callback == NULL);
	assert(M->callback_deletedto == NULL);

	/* Free our structure. */
	free(M);
}

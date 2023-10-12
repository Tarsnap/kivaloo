#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "entropy.h"
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
	int (* callback_state)(void *);
	void * cookie_state;
};

/* Internal state. */
struct metadata {
	struct wire_requestqueue * Q;
	struct mtuple M_stored;
	struct mtuple M_storing;
	struct mtuple M_latest;
	uint8_t process_id[32];
	int (* callback_deletedto)(void *);
	void * cookie_deletedto;
	int write_inprogress;
	int write_wanted;
	int init_done;
	int init_lostrace;
	uint64_t itemsz;
	uint8_t tableid[32];
};

static int callback_readmetadata(void *, int, const uint8_t *, size_t);
static int callback_claimmetadata(void *, int);
static int callback_writemetadata(void *, int);

static int
callback_readmetadata(void * cookie, int status,
    const uint8_t * buf, size_t len)
{
	struct metadata * M = cookie;
	uint8_t nbuf[104];

	/* Failures are bad. */
	if (status == 1)
		goto err0;

	/* Did the item exist? */
	if (status == 2) {
		warnp("metadata table is not initialized");
		goto err0;
	}

	/* We should have 104 bytes. */
	if (len != 104) {
		warn0("metadata has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	M->M_stored.nextblk = be64dec(&buf[0]);
	M->M_stored.deletedto = be64dec(&buf[8]);
	M->M_stored.generation = be64dec(&buf[16]);
	M->M_stored.lastblk = be64dec(&buf[24]);
	M->itemsz = be64dec(&buf[64]);
	memcpy(M->tableid, &buf[72], 32);

	/* Generate a random process ID. */
	if (entropy_read(M->process_id, 32)) {
		warn0("Failed to generate random process ID");
		goto err0;
	}

	/* Write new metadata back. */
	memcpy(nbuf, buf, 104);
	memcpy(&nbuf[32], M->process_id, 32);
	if (proto_dynamodb_kv_request_icas(M->Q, "metadata", buf, 104,
	    nbuf, 104, callback_claimmetadata, M))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_claimmetadata(void * _m, int status)
{
	struct metadata * M = _m;

	/* Did we succeed? */
	switch (status) {
	case 0:
		/* Request succeeded and we won the race. */
		M->init_lostrace = 0;
		break;
	case 1:
		/* Request failed.  This is bad. */
		warn0("Failed to claim ownership of metadata!");
		goto err0;
	case 2:
		/* We lost the race. */
		M->init_lostrace = 1;
		break;
	}

	/* We're done. */
	M->init_done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
writemetadata(struct metadata * M)
{
	uint8_t obuf[104];
	uint8_t buf[104];

	/* Is a write already in progress? */
	if (M->write_inprogress == 1) {
		M->write_wanted = 1;
		return (0);
	}

	/* We're going to start a write now. */
	M->write_inprogress = 1;
	M->write_wanted = 0;

	/* We're going to store the latest metadata values. */
	M->M_storing = M->M_latest;

	/* The requested callback will be performed when the store completes. */
	M->M_latest.callback_state = NULL;

	/* Increment metadata generation for the next metadata stored. */
	M->M_latest.generation++;

	/* Encode metadata. */
	be64enc(&obuf[0], M->M_stored.nextblk);
	be64enc(&obuf[8], M->M_stored.deletedto);
	be64enc(&obuf[16], M->M_stored.generation);
	be64enc(&obuf[24], M->M_stored.lastblk);
	memcpy(&obuf[32], M->process_id, 32);
	be64enc(&obuf[64], M->itemsz);
	memcpy(&obuf[72], M->tableid, 32);
	be64enc(&buf[0], M->M_storing.nextblk);
	be64enc(&buf[8], M->M_storing.deletedto);
	be64enc(&buf[16], M->M_storing.generation);
	be64enc(&buf[24], M->M_storing.lastblk);
	memcpy(&buf[32], M->process_id, 32);
	be64enc(&buf[64], M->itemsz);
	memcpy(&buf[72], M->tableid, 32);

	/* Write metadata. */
	if (proto_dynamodb_kv_request_icas(M->Q, "metadata", obuf, 104,
	    buf, 104, callback_writemetadata, M))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_writemetadata(void * _m, int status)
{
	struct metadata * M = _m;
	int (* callback)(void *);
	void * cookie;
	int rc = 0;
	int rc2;

	/* Sanity-check. */
	assert(M->write_inprogress);

	/* Did we succeed? */
	switch (status) {
	case 0:
		/* Request succeeded. */
		break;
	case 1:
		/* Request failed.  This is bad. */
		warn0("Failed to store metadata to DynamoDB!");
		goto err0;
	case 2:
		/* Another process stole the metadata from us. */
		warn0("Lost ownership of metadata in DynamoDB!");

		/*
		 * We could error out here, but it's safer to just abort;
		 * another process stealing our metadata tells us that we
		 * should not do anything else at all.
		 */
		_exit(0);
	}

	/* We're no longer storing metadata. */
	M->write_inprogress = 0;

	/* The values we were storing have now been stored. */
	M->M_stored = M->M_storing;

	/* Perform callbacks as appropriate. */
	if (M->M_stored.callback_state != NULL) {
		callback = M->M_stored.callback_state;
		cookie = M->M_stored.cookie_state;
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
 * metadata_init(Q, itemsz, tableid):
 * Prepare for metadata operations using the queue ${Q}, and take ownership of
 * the metadata item.  This function may call events_run() internally.  Return
 * the DynamoDB item size via ${itemsz} and the table ID via ${tableid}.
 */
struct metadata *
metadata_init(struct wire_requestqueue * Q, uint64_t * itemsz,
    uint8_t * tableid)
{
	struct metadata * M;

	/* Bake a cookie. */
	if ((M = malloc(sizeof(struct metadata))) == NULL)
		goto err0;
	M->Q = Q;

	/* Read metadata and take ownership. */
tryagain:
	M->init_done = 0;
	if (proto_dynamodb_kv_request_getc(Q, "metadata",
	    callback_readmetadata, M)) {
		warnp("Error reading LBS metadata");
		goto err1;
	}
	if (events_spin(&M->init_done)) {
		warnp("Error claiming ownership of LBS metadata");
		goto err1;
	}

	/* Did we lose a race trying to claim the metadata? */
	if (M->init_lostrace) {
		warn0("Lost race claiming metadata; trying again...");
		goto tryagain;
	}

	/* The next metadata will be the same except one higher generation. */
	M->M_latest = M->M_stored;
	M->M_latest.generation++;

	/* Nothing in progress yet. */
	M->M_latest.callback_state = NULL;
	M->M_latest.cookie_state = NULL;
	M->callback_deletedto = NULL;
	M->cookie_deletedto = NULL;
	M->write_inprogress = 0;
	M->write_wanted = 0;

	/* Return the item size and table ID. */
	*itemsz = M->itemsz;
	memcpy(tableid, M->tableid, 32);

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
    int (* callback)(void *), void * cookie)
{

	/* We shouldn't have a callback already. */
	assert(M->M_latest.callback_state == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.nextblk = nextblk;
	M->M_latest.callback_state = callback;
	M->M_latest.cookie_state = cookie;

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
    int (* callback)(void *), void * cookie)
{

	/* We shouldn't have a callback already. */
	assert(M->M_latest.callback_state == NULL);

	/* Record the new value and callback parameters. */
	M->M_latest.lastblk = lastblk;
	M->M_latest.callback_state = callback;
	M->M_latest.cookie_state = cookie;

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
 * metadata_deletedto_write(M, deletedto):
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
    int (* callback)(void *), void * cookie)
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
	assert(M->M_latest.callback_state == NULL);
	assert(M->callback_deletedto == NULL);

	/* Free our structure. */
	free(M);
}

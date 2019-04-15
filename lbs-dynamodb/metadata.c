#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "proto_dynamodb_kv.h"
#include "sysendian.h"
#include "warnp.h"

#include "metadata.h"

/* Internal state. */
struct metadata {
	struct wire_requestqueue * Q;
};

/**
 * metadata_init(Q):
 * Prepare for metadata operations using the queue ${Q}.
 */
struct metadata *
metadata_init(struct wire_requestqueue * Q)
{
	struct metadata * M;

	/* Bake a cookie. */
	if ((M = malloc(sizeof(struct metadata))) == NULL)
		goto err0;
	M->Q = Q;

	/* Success! */
	return (M);

err0:
	/* Failure! */
	return (NULL);
}

/* Used for reading "nextblk" during initialization. */
struct readnextblk {
	uint64_t nextblk;
	int done;
};

static int
callback_readnextblk(void * cookie, int status,
    const uint8_t * buf, size_t len)
{
	struct readnextblk * R = cookie;

	/* Failures are bad. */
	if (status == 1)
		goto err0;

	/* Did the item exist? */
	if (status == 2) {
		/* Nothing written yet?  We'll start at block #0. */
		R->nextblk = 0;
		goto done;
	}

	/* We should have 8 bytes. */
	if (len != 8) {
		warn0("nextblk has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	R->nextblk = be64dec(buf);

done:
	R->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Used for reading "DeletedTo" during initialization. */
struct readdeletedto {
	uint64_t deletedto;
	int done;
};

/* Callback for reading DeletedTo. */
static int
callback_deletedto_get(void * cookie, int status,
    const uint8_t * buf, size_t len)
{
	struct readdeletedto * D = cookie;

	/* Failures are bad. */
	if (status == 1) {
		warn0("Error reading DeletedTo");
		goto err0;
	}

	/* Did the item exist? */
	if (status == 2) {
		/* That's fine; we haven't deleted anything yet. */
		D->deletedto = 0;
		goto done;
	}

	/* We should have 8 bytes. */
	if (len != 8) {
		warn0("DeletedTo has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	D->deletedto = be64dec(buf);

done:
	D->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_nextblk_read(M, nextblk):
 * Read the "nextblk" value.  This function may call events_run internally.
 */
int
metadata_nextblk_read(struct metadata * M, uint64_t * nextblk)
{
	struct readnextblk R;

	R.done = 0;
	if (proto_dynamodb_kv_request_getc(M->Q, "nextblk",
	    callback_readnextblk, &R) ||
	    events_spin(&R.done)) {
		warnp("Error reading nextblk");
		goto err0;
	}
	*nextblk = R.nextblk;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_nextblk_write(M, nextblk, callback, cookie):
 * Store "nextblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_nextblk_write(struct metadata * M, uint64_t nextblk,
    int (*callback)(void *, int), void * cookie)
{
	uint8_t nextblk_enc[8];

	be64enc(nextblk_enc, nextblk);
	if (proto_dynamodb_kv_request_put(M->Q, "nextblk", nextblk_enc, 8,
	    callback, cookie))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_deletedto_read(M, deletedto):
 * Read the "deletedto" value.  This function may call events_run internally.
 */
int
metadata_deletedto_read(struct metadata * M, uint64_t * deletedto)
{
	struct readdeletedto R;

	R.done = 0;
	if (proto_dynamodb_kv_request_get(M->Q, "DeletedTo",
	    callback_deletedto_get, &R) ||
	    events_spin(&R.done))
		goto err0;
	*deletedto = R.deletedto;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_deletedto_write(M, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_deletedto_write(struct metadata * M, uint64_t deletedto,
    int (*callback)(void *, int), void * cookie)
{
	uint8_t deletedto_enc[8];

	be64enc(deletedto_enc, deletedto);
	if (proto_dynamodb_kv_request_put(M->Q, "DeletedTo", deletedto_enc, 8,
	    callback, cookie))
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

	/* Free our structure. */
	free(M);
}

#include <stdint.h>

#include "events.h"
#include "proto_dynamodb_kv.h"
#include "sysendian.h"
#include "warnp.h"

#include "metadata.h"

/* Used for reading "lastblk" during initialization. */
struct readlastblk {
	uint64_t lastblk;
	int done;
};

static int
callback_readlastblk(void * cookie, int status,
    const uint8_t * buf, size_t len)
{
	struct readlastblk * R = cookie;

	/* Failures are bad. */
	if (status == 1)
		goto err0;

	/* Did the item exist? */
	if (status == 2) {
		/* That's fine; we have no blocks yet. */
		R->lastblk = (uint64_t)(-1);
		goto done;
	}

	/* We should have 8 bytes. */
	if (len != 8) {
		warn0("lastblk has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse it. */
	R->lastblk = be64dec(buf);

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
 * metadata_lastblk_read(Q, lastblk):
 * Read the "lastblk" value.  This function may call events_run internally.
 */
int
metadata_lastblk_read(struct wire_requestqueue * Q, uint64_t * lastblk)
{
	struct readlastblk R;

	R.done = 0;
	if (proto_dynamodb_kv_request_getc(Q, "lastblk",
	    callback_readlastblk, &R) ||
	    events_spin(&R.done)) {
		warnp("Error reading lastblk");
		goto err0;
	}
	*lastblk = R.lastblk;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_lastblk_write(Q, lastblk, callback, cookie):
 * Store "lastblk" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_lastblk_write(struct wire_requestqueue * Q, uint64_t lastblk,
    int (*callback)(void *, int), void * cookie)
{
	uint8_t lastblk_enc[8];

	be64enc(lastblk_enc, lastblk);
	if (proto_dynamodb_kv_request_put(Q, "lastblk", lastblk_enc, 8,
	    callback, cookie))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * metadata_deletedto_read(Q, lastblk):
 * Read the "deletedto" value.  This function may call events_run internally.
 */
int
metadata_deletedto_read(struct wire_requestqueue * Q, uint64_t * deletedto)
{
	struct readdeletedto R;

	R.done = 0;
	if (proto_dynamodb_kv_request_get(Q, "DeletedTo",
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
 * metadata_deletedto_write(Q, deletedto, callback, cookie):
 * Store "deletedto" value.  Invoke ${callback}(${cookie}) on success.
 */
int
metadata_deletedto_write(struct wire_requestqueue * Q, uint64_t deletedto,
    int (*callback)(void *, int), void * cookie)
{
	uint8_t deletedto_enc[8];

	be64enc(deletedto_enc, deletedto);
	if (proto_dynamodb_kv_request_put(Q, "DeletedTo", deletedto_enc, 8,
	    callback, cookie))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

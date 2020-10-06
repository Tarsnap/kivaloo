#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "objmap.h"
#include "proto_s3.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

#include "deleteto.h"

struct deleteto {
	struct wire_requestqueue * Q;
	char * bucket;
	uint64_t N;	/* Delete objects below this number. */
	uint64_t M;	/* We've deleted everything below this number. */
	int done;
	size_t npending;	/* S3 operations in progress. */
	int idle;	/* (npending == 0). */
	int updateDeletedTo;	/* Has M changed since we last stored it? */
	int shuttingdown;	/* Stop poking ourselves. */
};

static int callback_done(void *, int);

/* Callback for checking if DeletedMarker exists from deleteto_init. */
static int
callback_deletedmarker_head(void * cookie, int status, size_t len)
{
	struct deleteto * D = cookie;

	/* If we've got a 404, DeletedMarker is initialized to 1. */
	if (status == 404) {
		D->M = 1;
	} else if (status == 200) {
		/* If we have a 200, the length should be 8. */
		if (len != 8) {
			warn0("DeletedMarker has incorrect size: %zu", len);
			goto err0;
		}

		/* Set to 0 to mean "read the value". */
		D->M = 0;
	} else {
		warn0("Unexpected HEAD response code from S3: %d", status);
		goto err0;
	}

	/* We're done. */
	D->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback for reading DeletedMarker from deleteto_init. */
static int
callback_deletedmarker_get(void * cookie, int failed,
    size_t len, const uint8_t * buf)
{
	struct deleteto * D = cookie;

	/* If we failed to read the file, die. */
	if (failed) {
		warn0("Could not read DeletedMarker from S3");
		goto err0;
	}

	/* We should have 8 bytes. */
	if (len != 8) {
		warn0("DeletedMarker has incorrect size: %zu", len);
		goto err0;
	}

	/* Parse the value. */
	D->M = be64dec(buf);

	/* We're done. */
	D->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * deleteto_init(Q_S3, bucket):
 * Initialize the deleter to operate on bucket ${bucket} via the S3 daemon
 * connected to ${Q_S3}.  This function may call events_run() internally.
 */
struct deleteto *
deleteto_init(struct wire_requestqueue * Q_S3, const char * bucket)
{
	struct deleteto * D;

	/* Allocate and initialize a DeleteTo structure. */
	if ((D = malloc(sizeof(struct deleteto))) == NULL)
		goto err0;
	D->Q = Q_S3;
	D->N = 0;
	D->npending = 0;
	D->idle = 1;
	D->updateDeletedTo = 0;
	D->shuttingdown = 0;
	if ((D->bucket = strdup(bucket)) == NULL)
		goto err1;

	/* Check if a DeletedMarker exists (if not, we treat it as 1). */
	D->done = 0;
	if (proto_s3_request_head(D->Q, D->bucket, "DeletedMarker",
	    callback_deletedmarker_head, D))
		goto err2;
	if (events_spin(&D->done))
		goto err2;

	/*
	 * If a marker exists, read it.  Eventual consistency is fine here
	 * since at worst we'll get an old marker and re-issue some deletes.
	 */
	if (D->M == 0) {
		D->done = 0;
		if (proto_s3_request_get(D->Q, D->bucket, "DeletedMarker", 8,
		    callback_deletedmarker_get, D))
			goto err2;
		if (events_spin(&D->done))
			goto err2;
	}

	/* Success! */
	return (D);

err2:
	free(D->bucket);
err1:
	free(D);
err0:
	/* Failure! */
	return (NULL);
}

/* Do a round of deletes if appropriate. */
static int
poke(struct deleteto * D)
{
	uint64_t BIT;
	uint64_t X;
	uint8_t DeletedMarker[8];

	/* If we're trying to shut down, don't do anything. */
	if (D->shuttingdown)
		return (0);

	/* If operations are already in progress, don't do anything. */
	if (! D->idle)
		return (0);

	/* Sanity-check. */
	assert(D->npending == 0);

	/*
	 * Store the M to object DeletedMarker if it's a multiple of 256
	 * (periodic stores so DeletedMarker doesn't fall too far behind
	 * reality if we are doing a very large number of deletes) and we
	 * haven't yet stored this value of M.
	 *
	 * If we crash and restart, we may end up re-issuing as many as ~256
	 * deletes; but this is better than more-frequent updating of the
	 * deletion marker since (a) DELETEs are free but PUTs aren't, and
	 * (b) we want to optimize for the common case, which is a long-lived
	 * lbs-s3 process.
	 */
	if ((D->M % 256 == 0) && (D->updateDeletedTo == 1)) {
		D->idle = 0;
		D->npending += 1;
		be64enc(DeletedMarker, D->M);
		if (proto_s3_request_put(D->Q, D->bucket, "DeletedMarker",
		    8, DeletedMarker, callback_done, D))
			goto err0;
		D->updateDeletedTo = 0;
	}

	/* If we can't delete anything, don't. */
	if (D->N <= D->M)
		return (0);

	/*
	 * We want to run one step of the DeleteTo algorithm: Delete or
	 * overwrite objects which are needed by M but not by M+1, and
	 * increment M.  If nothing needs to be done, we'll repeat the
	 * process for the new (incremented) M.
	 */

	/* For each bit... */
	for (BIT = 1; BIT != 0; BIT += BIT) {
		/* If it's set in M but not in M+1... */
		if (((D->M & BIT) == BIT) &&
		    (((D->M + 1) & BIT) == 0)) {
			/* M - M % BIT is deletable... */
			X = D->M - (D->M % BIT);

			/* ... unless it's a power of two. */
			if (X == BIT)
				continue;

			/* Issue a delete. */
			D->idle = 0;
			D->npending += 1;
			if (proto_s3_request_delete(D->Q, D->bucket,
			    objmap(X), callback_done, D))
				goto err0;
		}
	}

	/*
	 * Powers of 2 will never be DELETEd, and multiples of 256 can't be
	 * deleted until at least 256 iterations later (since N = ...abcdefgh)
	 * needs the file ...00000000 to still exist), but we don't need the
	 * data for M any more; so issue an empty PUT for it if it falls into
	 * one of those two categories.
	 */
	if (((D->M & (D->M-1)) == 0) || ((D->M % 256) == 0)) {
		D->idle = 0;
		D->npending += 1;
		if (proto_s3_request_put(D->Q, D->bucket, objmap(D->M),
		    0, NULL, callback_done, D))
			goto err0;
	}

	/* We've issued all the deletes needed for this M. */
	D->M = D->M + 1;
	D->updateDeletedTo = 1;

	/* If we haven't found anything to do yet, poke ourselves again. */
	if (D->idle)
		return (poke(D));

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* One of the S3 operations kicked off by poke() has completed. */
static int
callback_done(void * cookie, int failed)
{
	struct deleteto * D = cookie;

	/* Sanity-checks. */
	assert(D->idle == 0);
	assert(D->npending > 0);

	/* Failures are bad, m'kay? */
	if (failed)
		goto err0;

	/* We've finished an operation. */
	D->npending -= 1;

	/* Have we finished all of them? */
	if (D->npending == 0) {
		D->idle = 1;
		if (poke(D))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * deleteto_deleteto(D, N):
 * S3 objects with numbers less than ${N} are no longer needed by the B+Tree.
 * Inform the deleteto state ${D} which may opt to do something about them.
 */
int
deleteto_deleteto(struct deleteto * D, uint64_t N)
{

	/* Record the new DeleteTo value. */
	if (D->N < N)
		D->N = N;

	/* Start doing stuff if necessary. */
	return (poke(D));
}

/**
 * deleteto_stop(D):
 * Clean up, shut down, and free the deleteto state ${D}.  This function may
 * call events_run() internally.
 */
int
deleteto_stop(struct deleteto * D)
{
	int rc;

	/* We don't want to do any more DELETEs, just shut down. */
	D->shuttingdown = 1;

	/* Wait for S3 operations to finish. */
	rc = events_spin(&D->idle);

	/* Free the DeleteTo structure. */
	free(D->bucket);
	free(D);

	/* Return status from events_spin. */
	return (rc);
}

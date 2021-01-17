#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "metadata.h"
#include "objmap.h"
#include "proto_dynamodb_kv.h"
#include "warnp.h"

#include "deleteto.h"

/* Maximum number of deletes in progress at once. */
#define MAXINPROGRESS	64

/* Maximum number of deletes to replay if we crash. */
#define MAXUNRECORDED	8000

struct delete {
	struct deleteto * D;
	uint64_t N;
	struct delete * prev;
	struct delete * next;
};

struct deleteto {
	struct wire_requestqueue * Q;
	struct metadata * MD;
	uint64_t N;		/* Delete objects below this number. */
	uint64_t M;		/* We've issued deletes up to this number. */
	size_t npending;	/* DELETE operations in progress. */
	int shuttingdown;	/* Stop issuing DELETEs. */
	int shutdown;		/* Everything is done. */
	struct delete * ip_head;	/* Head of deletes in progress list. */
	struct delete * ip_tail;	/* Tail of deletes in progress list. */
};

static int poke(void *);
static int callback_done(void *, int);

/**
 * deleteto_init(Q_DDBKV, M):
 * Initialize the deleter to operate via the DynamoDB-KV daemon connected to
 * ${Q_DDBKV} and the metadata handler M.
 */
struct deleteto *
deleteto_init(struct wire_requestqueue * Q_DDBKV, struct metadata * M)
{
	struct deleteto * D;

	/* Allocate and initialize a DeleteTo structure. */
	if ((D = malloc(sizeof(struct deleteto))) == NULL)
		goto err0;
	D->Q = Q_DDBKV;
	D->MD = M;
	D->N = 0;
	D->npending = 0;
	D->shuttingdown = 0;
	D->shutdown = 0;
	D->ip_head = NULL;
	D->ip_tail = NULL;

	/* How far are we guaranteed was previously deleted? */
	D->M = metadata_deletedto_read(D->MD);

	/* We want to be poked every time a metadata write completes. */
	metadata_deletedto_register(D->MD, poke, D);

	/* Success! */
	return (D);

err0:
	/* Failure! */
	return (NULL);
}

/* Do a round of deletes if appropriate. */
static int
poke(void * cookie)
{
	struct deleteto * D = cookie;
	struct delete * D1;
	uint64_t deletedto;

	/* Tell the metadata code how far we've finished deleting. */
	if (D->ip_head != NULL)
		deletedto = D->ip_head->N;
	else
		deletedto = D->M;
	if (metadata_deletedto_write(D->MD, deletedto))
		goto err0;

	/* Are we waiting to shut down? */
	if (D->shuttingdown) {
		/*
		 * If there's no deletes in progress and the metadata is up to
		 * date with how far we've deleted, we're done.
		 */
		if ((D->npending == 0) &&
		    (metadata_deletedto_read(D->MD) == deletedto))
			D->shutdown = 1;

		/* Either way, return without sending any more. */
		return (0);
	}

	/* Can we issue more deletes? */
	while ((D->M < D->N) &&
	    (D->npending < MAXINPROGRESS) &&
	    (D->M < metadata_deletedto_read(D->MD) + MAXUNRECORDED)) {
		/* Bake a cookie for deleting M. */
		if ((D1 = malloc(sizeof(struct delete))) == NULL)
			goto err0;
		D1->D = D;
		D1->N = D->M;
		D1->prev = D->ip_tail;
		D1->next = NULL;
		if (D->ip_head == NULL)
			D->ip_head = D1;
		else
			D->ip_tail->next = D1;
		D->ip_tail = D1;

		/* Issue the delete. */
		if (proto_dynamodb_kv_request_delete(D->Q, objmap(D->M),
		    callback_done, D1))
			goto err0;
		D->npending++;

		/* We've issued deletes for everything under one more. */
		D->M++;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* A delete has completed. */
static int
callback_done(void * cookie, int status)
{
	struct delete * D1 = cookie;
	struct deleteto * D = D1->D;

	/* Sanity-check. */
	assert(D->npending > 0);

	/* Failures are bad, m'kay? */
	if (status) {
		warn0("DynamoDB-KV DELETE operation failed!");
		goto err0;
	}

	/* Remove from linked list of in-progress deletes. */
	if (D1->prev != NULL)
		D1->prev->next = D1->next;
	else
		D->ip_head = D1->next;
	if (D1->next != NULL)
		D1->next->prev = D1->prev;
	else
		D->ip_tail = D1->prev;

	/* Free cookie. */
	free(D1);

	/* We've finished an operation. */
	D->npending -= 1;

	/* Check what we should do next. */
	if (poke(D))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * deleteto_deleteto(D, N):
 * Pages with numbers less than ${N} are no longer needed by the B+Tree.
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
 * call events_run internally.
 */
int
deleteto_stop(struct deleteto * D)
{

	/* We don't want to do any more DELETEs, just shut down. */
	D->shuttingdown = 1;

	/* Poke the deleter in case it's not already doing anything. */
	if (poke(D))
		goto err0;

	/* Wait for all pending operations to finish. */
	if (events_spin(&D->shutdown))
		goto err0;

	/* We don't want to know about metadata writes completing. */
	metadata_deletedto_register(D->MD, NULL, NULL);

	/* Free the DeleteTo structure. */
	free(D);

	/* Success! */
	return (0);

	/*
	 * We don't free D in the error path, in case there are pending
	 * callbacks holding references to it; until D->shutdown is nonzero
	 * we can't assume that all operations have completed.
	 */
err0:
	/* Failure! */
	return (-1);
}

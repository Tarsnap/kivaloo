#include <stdlib.h>

#include "proto_lbs.h"
#include "warnp.h"

#include "dispatch.h"
#include "storage.h"
#include "worker.h"

#include "dispatch_internal.h"

/**
 * dispatch_request_params(dstate, R):
 * Handle and free a PARAMS request.
 */
int
dispatch_request_params(struct dispatch_state * dstate,
    struct proto_lbs_request * R)
{
	uint64_t blkno;

	/* Figure out what the first available block number is. */
	if ((blkno = storage_nextblock(dstate->sstate)) == (uint64_t)(-1))
		goto err1;

	/* Send the response packet back. */
	dstate->npending--;
	if (proto_lbs_response_params(dstate->writeq, R->ID,
	    dstate->blocklen, blkno))
		goto err1;

	/* Free the request structure. */
	free(R);

	/* Success! */
	return (0);

err1:
	free(R);

	/* Failure! */
	return (-1);
}

/**
 * dispatch_request_get(dstate, R):
 * Handle and free a GET request (queue it if necessary).
 */
int
dispatch_request_get(struct dispatch_state * dstate,
    struct proto_lbs_request * R)
{
	struct readq * rq;

	/* Add the read request to the pending read queue. */
	if ((rq = malloc(sizeof(struct readq))) == NULL)
		goto err1;
	rq->next = NULL;
	rq->reqID = R->ID;
	rq->blkno = R->r.get.blkno;
	if (dstate->readq_head == NULL)
		dstate->readq_head = rq;
	else
		*(dstate->readq_tail) = rq;
	dstate->readq_tail = &rq->next;

	/* Free the request structure. */
	free(R);

	/* Poke the queue. */
	if (dispatch_request_pokereadq(dstate))
		goto err0;

	/* Success! */
	return (0);

err1:
	free(R);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_request_pokereadq(dstate):
 * Launch queued GET(s) if possible.
 */
int
dispatch_request_pokereadq(struct dispatch_state * dstate)
{
	struct readq * R;
	struct workctl * reader;
	uint8_t * buf;

	/* Loop as long as we can launch a read. */
	while ((dstate->nreaders_idle > 0) && (dstate->readq_head != NULL)) {
		/* Grab the first read from the queue. */
		R = dstate->readq_head;

		/* Allocate a buffer to read the block into. */
		if ((buf = malloc(dstate->blocklen)) == NULL)
			goto err0;

		/* Grab an idle reader. */
		reader = dstate->workers[
		    dstate->readers_idle[dstate->nreaders_idle - 1]];
		dstate->nreaders_idle -= 1;

		/* Give the reader the work. */
		if (worker_assign(reader, 0, R->blkno, 0, buf, R->reqID))
			goto err1;

		/* Remove the work from the queue. */
		dstate->readq_head = R->next;

		/* Free the dequeued read queue entry. */
		free(R);
	}

	/* Success! */
	return (0);

err1:
	dstate->nreaders_idle += 1;
	free(buf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * dispatch_request_append(dstate, R):
 * Handle and free a APPEND request.
 */
int
dispatch_request_append(struct dispatch_state * dstate,
    struct proto_lbs_request * R)
{
	struct workctl * writer = dstate->workers[dstate->nreaders];
	uint64_t blkno;

	/* Figure out what the first available block number is. */
	if ((blkno = storage_nextblock(dstate->sstate)) == (uint64_t)(-1))
		goto err1;

	/*
	 * If the block number provided is wrong, or there's a write in
	 * progress (in which case the requestor can't possibly know what
	 * the correct next block number is), send a failure response.
	 */
	if ((R->r.append.blkno != blkno) || (dstate->writer_busy != 0)) {
		dstate->npending--;
		if (proto_lbs_response_append(dstate->writeq, R->ID, 1,
		    (uint64_t)(-1)))
			goto err1;
	}

	/* Give the writer the work. */
	dstate->writer_busy = 1;
	if (worker_assign(writer, 1, R->r.append.blkno, R->r.append.nblks,
	    R->r.append.buf, R->ID))
		goto err1;

	/* Free the request but NOT the buffer, since the thread owns that. */
	free(R);

	/* Success! */
	return (0);

err1:
	/* Free request AND included buffer. */
	free(R->r.append.buf);
	free(R);

	/* Failure! */
	return (-1);
}

/**
 * dispatch_request_free(dstate, R):
 * Handle and free a FREE request.
 */
int
dispatch_request_free(struct dispatch_state * dstate,
    struct proto_lbs_request * R)
{
	struct workctl * deleter = dstate->workers[dstate->nreaders + 1];

	/* If the deleter is not busy, tell it to do something. */
	if (dstate->deleter_busy == 0) {
		/* Give the deleter the work. */
		dstate->deleter_busy = 1;
		if (worker_assign(deleter,
		    2, R->r.free.blkno, 0, NULL, R->ID))
			goto err1;
	}

	/*
	 * Send an ACK to the request.  FREEs are advisory, so we don't need
	 * to wait until we succeed before responding.
	 */
	dstate->npending--;
	if (proto_lbs_response_free(dstate->writeq, R->ID))
		goto err1;

	/* Free the request. */
	free(R);

	/* Success! */
	return (0);

err1:
	free(R);

	/* Failure! */
	return (-1);
}

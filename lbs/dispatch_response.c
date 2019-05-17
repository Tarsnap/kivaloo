#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "proto_lbs.h"
#include "warnp.h"

#include "dispatch.h"
#include "storage.h"
#include "worker.h"

#include "dispatch_internal.h"

/**
 * dispatch_response_send(dstate, thread):
 * Using the dispatch state ${dstate}, send a response for the work which was
 * just completed by thread ${thread}.
 */
int
dispatch_response_send(struct dispatch_state * dstate, struct workctl * thread)
{
	int op;
	uint64_t blkno;
	size_t nblks;
	uint8_t * buf;
	uint64_t reqID;
	int status;

	/* Figure out what work was completed. */
	if (worker_getdone(thread, &op, &blkno, &nblks, &buf, &reqID))
		goto err0;

	/* Different types of work get handled differently. */
	switch (op) {
	case 0:	/* read operation. */
		/* Sanity check. */
		assert(dstate->blocklen <= UINT32_MAX);

		/* If we read a block, our status is 0; otherwise, 1. */
		if (nblks == 1)
			status = 0;
		else
			status = 1;

		/* Send a response. */
		dstate->npending--;
		if (proto_lbs_response_get(dstate->writeq, reqID, status,
		    (uint32_t)dstate->blocklen, buf))
			goto err1;

		/* Free the buffer holding read data. */
		free(buf);

		break;
	case 1:	/* write operation. */
		/* Figure out what the next available block number is. */
		if ((blkno = storage_nextblock(dstate->sstate)) ==
		    (uint64_t)(-1))
			goto err1;

		/*
		 * Send a response back, with status = 0 since we will only
		 * end up here if the requested write position was correct.
		 */
		dstate->npending--;
		if (proto_lbs_response_append(dstate->writeq, reqID, 0, blkno))
			goto err1;

		/* Free the buffer holding written data. */
		free(buf);

		break;
	case 2:	/* delete operation. */
		/*
		 * We have nothing to do in this case, since the FREE command
		 * is merely advisory and a response was sent from request.c
		 * before the work was assigned to a thread.
		 */
		break;
	default:
		warn0("invalid work type: %d", op);
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	free(buf);
err0:
	/* Failure! */
	return (-1);
}

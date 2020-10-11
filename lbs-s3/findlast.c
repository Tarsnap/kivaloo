#include <stddef.h>
#include <stdint.h>

#include "events.h"
#include "proto_s3.h"
#include "warnp.h"
#include "wire.h"

#include "objmap.h"

#include "findlast.h"

struct headdata {
	int done;
	int status;
	size_t clen;
};

/* Callback for HEAD requests. */
static int
callback_head(void * cookie, int status, size_t len)
{
	struct headdata * hd = cookie;

	/* Record callback data. */
	hd->status = status;
	hd->clen = len;

	/* This request is done. */
	hd->done = 1;

	/* Success! */
	return (0);
}

/**
 * head(Q_S3, bucket, N, status, clen):
 * Issue a HEAD request for object #${N} in bucket ${bucket} via the S3 daemon
 * connected to ${Q_S3}.  Wait until done.  Return the status via ${status}
 * and the Content-Length via ${clen}.
 */
static int
head(struct wire_requestqueue * Q_S3, const char * bucket, uint64_t N,
    int * status, size_t * clen)
{
	struct headdata hd;

	/* Issue a HEAD request. */
	hd.done = 0;
	if (proto_s3_request_head(Q_S3, bucket, objmap(N), callback_head, &hd))
		goto err1;

	/* Wait for the request to finish. */
	if (events_spin(&hd.done))
		goto err1;

	/* If the status is 0, we failed. */
	if (status == 0)
		goto err1;

	/* Return results. */
	*status = hd.status;
	*clen = hd.clen;

	/* Success! */
	return (0);

err1:
	warnp("Error issuing HEAD request");

	/* Failure! */
	return (-1);
}

/**
 * findlast(Q_S3, bucket, L, olen):
 * Using the S3 daemon connected to ${Q_S3}, find the number of the last
 * (non-empty) object in the S3 bucket ${bucket} and store it to ${L}.  Store
 * the size of that object into ${olen}.  If there are no numbered objects,
 * return L = 0, olen = 0.
 *
 * This function may call events_run() internally.
 */
int
findlast(struct wire_requestqueue * Q_S3, const char * bucket,
    uint64_t * L, size_t * olen)
{
	uint64_t N;
	int i;
	int status;
	size_t clen;

	/* We have no objects yet. */
	*L = 0;
	*olen = 0;

	/* See Algorithm FindLast in the DESIGN file. */
	for (i = 0; i < 64; i++) {
		if (head(Q_S3, bucket, (uint64_t)(1) << i, &status, &clen))
			goto err0;
		if (status == 404)
			break;
		if (status != 200)
			goto err1;
		*L = (uint64_t)(1) << i;
		*olen = clen;
	}
	for (N = *L / 2; N > 0; N = N / 2) {
		if (head(Q_S3, bucket, *L + N, &status, &clen))
			goto err0;
		if (status == 200) {
			*L = *L + N;
			*olen = clen;
		} else if (status != 404)
			goto err1;
	}

	/* If necessary, scan backwards until we find a non-empty object. */
	while ((*olen == 0) && (*L > 1)) {
		*L -= 1;
		if (head(Q_S3, bucket, *L, &status, &clen))
			goto err0;
		if (status == 404) {
			warn0("Cannot find non-empty S3 object");
			goto err0;
		} else if (status != 200)
			goto err1;
		*olen = clen;
	}

	/* Success! */
	return (0);

err1:
	warn0("HEAD returned status %d!", status);
err0:
	/* Failure! */
	return (-1);
}

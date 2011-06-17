#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "network.h"

#include "netbuf.h"

/* Buffered reader structure. */
struct netbuf_read {
	/* Reader state. */
	int s;			/* Source for reads. */
	int extread;		/* Is read into external buffer? */
	void * read_cookie;	/* Cookie for current buffer read. */
	void * immediate_cookie;/* Cookie for immediate callback. */

	/* Buffer state. */
	uint8_t * buf;		/* Internal buffer. */
	size_t buflen;		/* Length of buf. */
	size_t bufpos;		/* Current position within buffer. */
	size_t datalen;		/* # bytes of data in buffer. */

	/* Current request. */
	uint8_t * reqbuf;	/* Buffer data will end up in. */
	size_t reqbuflen;	/* Length of reqbuf. */
	size_t reqbufpos;	/* Current write position in reqbuf. */
	int (* callback)(void *, int);	/* Completion callback. */
	void * cookie;		/* Completion callback cookie. */
};

/* Copy data from R->buf into R->reqbuf. */
static void
copybuf(struct netbuf_read * R)
{
	size_t copylen;

	/* Figure out how much we can copy. */
	copylen = R->datalen;
	if (R->reqbuflen - R->reqbufpos < copylen)
		copylen = R->reqbuflen - R->reqbufpos;

	/* Copy the data. */
	memcpy(&R->reqbuf[R->reqbufpos], &R->buf[R->bufpos], copylen);
	R->reqbufpos += copylen;
	R->bufpos += copylen;
	R->datalen -= copylen;

	/* Reset buffer pointer if we have no data left. */
	if (R->datalen == 0)
		R->bufpos = 0;
}

/* Immediate callback for buffer read completion. */
static int
docallback(void * cookie)
{
	struct netbuf_read * R = cookie;

	/* This callback is no longer pending. */
	R->immediate_cookie = NULL;

	/* We no longer have a read in progress. */
	R->reqbuf = NULL;

	/* Do the callback and return. */
	return ((R->callback)(R->cookie, 0));
}

/* Callback for a read completion. */
static int
gotbuf(void * cookie, size_t lenread)
{
	struct netbuf_read * R = cookie;

	/* This callback is no longer pending. */
	R->read_cookie = NULL;

	/* Did we succeed? */
	if (lenread == 0)
		goto failed;

	/* We have data! */
	if (R->extread) {
		/* The data went into the external buffer. */
		R->reqbufpos += lenread;
	} else {
		/* The data went into our internal buffer. */
		R->datalen += lenread;

		/* Copy data into the request buffer. */
		copybuf(R);
	}

	/* We should have finished handling this request. */
	assert(R->reqbufpos == R->reqbuflen);

	/* We no longer have a read in progress. */
	R->reqbuf = NULL;

	/* Do the callback and return. */
	return ((R->callback)(R->cookie, 0));

failed:
	/* Perform a failure-callback and return. */
	return ((R->callback)(R->cookie, 1));
}

/**
 * netbuf_read_init(s):
 * Create and return a buffered reader attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to read from said
 * socket except via the returned reader.
 */
struct netbuf_read *
netbuf_read_init(int s)
{
	struct netbuf_read * R;

	/* Bake a cookie. */
	if ((R = malloc(sizeof(struct netbuf_read))) == NULL)
		goto err0;
	R->s = s;
	R->read_cookie = NULL;
	R->immediate_cookie = NULL;
	R->reqbuf = NULL;
	R->buflen = 4096;

	/* Allocate buffer. */
	if ((R->buf = malloc(R->buflen)) == NULL)
		goto err1;
	R->bufpos = 0;
	R->datalen = 0;

	/* Success! */
	return (R);

err1:
	free(R);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * netbuf_read_read(R, buf, buflen, callback, cookie):
 * Read ${buflen} bytes into the buffer ${buf} via the buffered reader ${R}.
 * Invoke ${callback}(${cookie}, status) when done, with status set to 0 on
 * success, and set to 1 on failure.
 */
int
netbuf_read_read(struct netbuf_read * R, uint8_t * buf, size_t buflen,
    int (* callback)(void *, int), void * cookie)
{

	/* Assert that we're not already filling a buffer. */
	assert(R->reqbuf == NULL);

	/* Store read request parameters. */
	R->reqbuf = buf;
	R->reqbuflen = buflen;
	R->reqbufpos = 0;
	R->callback = callback;
	R->cookie = cookie;

	/* Copy data if we have any. */
	copybuf(R);

	/*
	 * There are three cases to consider here:
	 * 1. We have filled the request buffer.  All we need to do is
	 * schedule an immediate callback.
	 * 2. There is no data left in our internal buffer, and the remaining
	 * space in the request buffer is LESS than the size of our internal
	 * buffer.  Read into our internal buffer (then copy data).
	 * 3. There is no data left in our internal buffer, and the remaining
	 * space in the request buffer is MORE than the size of our internal
	 * buffer.  Read directly into the request buffer.
	 */
	if (R->reqbufpos == R->reqbuflen) {
		/* Case 1: Schedule an immediate callback. */
		R->immediate_cookie =
		    events_immediate_register(docallback, R, 0);
	} else if ((R->reqbuflen - R->reqbufpos) < R->buflen) {
		/* Sanity check. */
		assert(R->bufpos == 0);

		/* Case 2: Read into internal buffer. */
		R->extread = 0;
		R->read_cookie = network_read(R->s, R->buf, R->buflen,
		    R->reqbuflen - R->reqbufpos, gotbuf, R);
	} else {
		/* Sanity check. */
		assert(R->bufpos == 0);

		/* Case 3: Read into external buffer. */
		R->extread = 1;
		R->read_cookie = network_read(R->s, &R->reqbuf[R->reqbufpos],
		    R->reqbuflen - R->reqbufpos,
		    R->reqbuflen - R->reqbufpos, gotbuf, R);
	}

	/* Did we succeed? */
	if ((R->immediate_cookie == NULL) && (R->read_cookie == NULL))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * netbuf_read_cancel(R):
 * Cancel the in-progress read on the reader ${R}.  Do not invoke the
 * callback associated with the read.
 */
void
netbuf_read_cancel(struct netbuf_read * R)
{

	/* If we have an in-progress read, cancel it. */
	if (R->read_cookie != NULL) {
		network_read_cancel(R->read_cookie);
		R->read_cookie = NULL;
	}

	/* If we have an immediate callback pending, cancel it. */
	if (R->immediate_cookie != NULL) {
		events_immediate_cancel(R->immediate_cookie);
		R->immediate_cookie = NULL;
	}
}

/**
 * netbuf_read_free(R):
 * Free the reader ${R}.  Note that an indeterminate amount of data may have
 * been buffered and will be lost.
 */
void
netbuf_read_free(struct netbuf_read * R)
{

	/* Can't free a reader which is busy. */
	assert(R->read_cookie == NULL);
	assert(R->immediate_cookie == NULL);

	/* Free the buffer and the reader. */
	free(R->buf);
	free(R);
}

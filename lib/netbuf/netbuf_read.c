#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "network.h"

#include "netbuf.h"

/* Buffered reader structure. */
struct netbuf_read {
	/* Reader state. */
	int s;				/* Source for reads. */
	int (* callback)(void *, int);	/* Callback for _wait. */
	void * cookie;			/* Cookie for _wait. */
	void * read_cookie;		/* From network_read. */
	void * immediate_cookie;	/* From events_immediate_register. */

	/* Buffer state. */
	uint8_t * buf;			/* Current read buffer. */
	size_t buflen;			/* Length of buf. */
	size_t bufpos;			/* Position of read pointer in buf. */
	size_t datalen;			/* Position of write pointer in buf. */

	/* Used by netbuf_read_read. */
	int (* rr_callback)(void *, int);	/* Callback function. */
	void * rr_cookie;			/* Cookie for callback. */
	uint8_t * rr_buf;			/* Buffer. */
	size_t rr_buflen;			/* Buffer size. */
};

static int callback_success(void *);
static int callback_read(void *, ssize_t);
static int callback_read_read(void *, int);

/**
 * netbuf_read_init(s):
 * Create and return a buffered reader attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to read from said
 * socket except via the returned reader.
 */
struct
netbuf_read * netbuf_read_init(int s)
{
	struct netbuf_read * R;

	/* Bake a cookie. */
	if ((R = malloc(sizeof(struct netbuf_read))) == NULL)
		goto err0;
	R->s = s;
	R->read_cookie = NULL;
	R->immediate_cookie = NULL;

	/* Allocate buffer. */
	R->buflen = 4096;
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
 * netbuf_read_peek(R, data, datalen):
 * Set ${data} to point to the currently buffered data in the reader ${R}; set
 * ${datalen} to the number of bytes buffered.
 */
void
netbuf_read_peek(struct netbuf_read * R, uint8_t ** data, size_t * datalen)
{

	/* Point at current buffered data. */
	*data = &R->buf[R->bufpos];
	*datalen = R->datalen - R->bufpos;
}

/**
 * netbuf_read_wait(R, len, callback, cookie):
 * Wait until ${R} has ${len} or more bytes of data buffered or an error
 * occurs; then invoke ${callback}(${cookie}, status) with status set to 0
 * if the data is available, and set to 1 on error.
 */
int
netbuf_read_wait(struct netbuf_read * R, size_t len,
    int (* callback)(void *, int), void * cookie)
{
	uint8_t * nbuf;
	size_t nbuflen;

	/* Sanity-check: We shouldn't be reading already. */
	assert(R->read_cookie == NULL);
	assert(R->immediate_cookie == NULL);

	/* Record parameters for future reference. */
	R->callback = callback;
	R->cookie = cookie;

	/* If we have enough data already, schedule a callback. */
	if (R->datalen - R->bufpos >= len) {
		if ((R->immediate_cookie =
		    events_immediate_register(callback_success, R, 0)) == NULL)
			goto err0;
		else
			goto done;
	}

	/* Resize the buffer if needed. */
	if (R->buflen < len) {
		/* Compute new buffer size. */
		nbuflen = R->buflen * 2;
		if (nbuflen < len)
			nbuflen = len;

		/* Allocate new buffer. */
		if ((nbuf = malloc(nbuflen)) == NULL)
			goto err0;

		/* Copy data into new buffer. */
		memcpy(nbuf, &R->buf[R->bufpos], R->datalen - R->bufpos);

		/* Free old buffer and use new buffer. */
		free(R->buf);
		R->buf = nbuf;
		R->buflen = nbuflen;
		R->datalen -= R->bufpos;
		R->bufpos = 0;
	}

	/* Move data to start of buffer if needed. */
	if (R->buflen - R->bufpos < len) {
		memmove(R->buf, &R->buf[R->bufpos], R->datalen - R->bufpos);
		R->datalen -= R->bufpos;
		R->bufpos = 0;
	}

	/* Read data into the buffer. */
	if ((R->read_cookie = network_read(R->s, &R->buf[R->datalen],
	    R->buflen - R->datalen, R->bufpos + len - R->datalen,
	    callback_read, R)) == NULL)
		goto err0;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Perform immediate callback for netbuf_read_wait. */
static int
callback_success(void * cookie)
{
	struct netbuf_read * R = cookie;

	/* Sanity-check: We should be expecting this callback. */
	assert(R->immediate_cookie != NULL);

	/* This callback is no longer pending. */
	R->immediate_cookie = NULL;

	/* Perform callback. */
	return ((R->callback)(R->cookie, 0));
}

/* Callback for a completed network read. */
static int
callback_read(void * cookie, ssize_t lenread)
{
	struct netbuf_read * R = cookie;

	/* Sanity-check: We should be reading. */
	assert(R->read_cookie != NULL);

	/* This callback is no longer pending. */
	R->read_cookie = NULL;

	/* Did the read fail?  Don't care about error vs. EOF. */
	if (lenread <= 0)
		goto failed;

	/* We've got more data. */
	R->datalen += lenread;

	/* Perform callback. */
	return ((R->callback)(R->cookie, 0));

failed:
	/* Perform failure callback. */
	return ((R->callback)(R->cookie, 1));
}

/**
 * netbuf_read_wait_cancel(R):
 * Cancel the in-progress wait on the reader ${R}.  Do not invoke the callback
 * associated with the wait.
 */
void
netbuf_read_wait_cancel(struct netbuf_read * R)
{

	/* Sanity-check: There should be a callback pending. */
	assert((R->read_cookie != NULL) || (R->immediate_cookie != NULL));

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
 * netbuf_read_consume(R, len):
 * Advance the reader pointer for the reader ${R} by ${len} bytes.
 */
void
netbuf_read_consume(struct netbuf_read * R, size_t len)
{

	/* Sanity-check: We can't consume data we don't have. */
	assert(R->datalen - R->bufpos >= len);

	/* Advance the buffer pointer. */
	R->bufpos += len;
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

	/* Store read request parameters. */
	R->rr_buf = buf;
	R->rr_buflen = buflen;
	R->rr_callback = callback;
	R->rr_cookie = cookie;

	/* Wait until we have the required amount of data. */
	return (netbuf_read_wait(R, buflen, callback_read_read, R));
}

/* Copy data and perform the upstream callback. */
static int
callback_read_read(void * cookie, int status)
{
	struct netbuf_read * R = cookie;
	uint8_t * data;
	size_t datalen;

	/* If we succeeded, copy the data. */
	if (status == 0) {
		/* Where's the data? */
		netbuf_read_peek(R, &data, &datalen);

		/* We'd better have enough of it. */
		assert(datalen >= R->rr_buflen);

		/* Copy it. */
		memcpy(R->rr_buf, data, R->rr_buflen);

		/* We've used this data. */
		netbuf_read_consume(R, R->rr_buflen);
	}

	/* Perform the upstream callback. */
	return ((R->rr_callback)(R->rr_cookie, status));
}

/**
 * netbuf_read_cancel(R):
 * Cancel the in-progress read on the reader ${R}.  Do not invoke the
 * callback associated with the read.
 */
void
netbuf_read_cancel(struct netbuf_read * R)
{

	/* Cancel the wait. */
	netbuf_read_wait_cancel(R);
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "network.h"
#include "warnp.h"

#include "netbuf.h"

#define WBUFLEN	4096

/* Linked list of write buffers. */
struct writebuf {
	uint8_t * buf;			/* The buffer to be written. */
	size_t buflen;			/* Size of buffer. */
	size_t datalen;			/* Amount of data in buffer. */
	struct writebuf * next;		/* Next buffer in queue. */
};

/* Buffered writer structure. */
struct netbuf_write {
	int s;				/* Destination for writes. */

	/* Failure handling. */
	int failed;			/* Has a write ever failed? */
	int (*fail_callback)(void *);	/* Callback to invoke on failure. */
	void * fail_cookie;		/* Cookie for failure callback. */

	/* Queued buffers. */
	struct writebuf * head;		/* Queue of buffers to write. */
	struct writebuf * tail;		/* Last buffer in queue. */

	/* Current write. */
	void * write_cookie;		/* Cookie from network_write. */
	struct writebuf * curr;		/* Buffer being written. */
};

static int writbuf(void *, ssize_t);
static int poke(struct netbuf_write *);

/* A buffer has been written. */
static int
writbuf(void * cookie, ssize_t writelen)
{
	struct netbuf_write * W = cookie;
	struct writebuf * WB = W->curr;

	/* Sanity-check: We must have had a write in progress. */
	assert(W->write_cookie != NULL);
	assert(W->curr != NULL);

	/* This write is no longer in progress. */
	W->write_cookie = NULL;
	W->curr = NULL;

	/* Sanity-check: We can't get here if we've previously failed. */
	assert(W->failed == 0);

	/*
	 * If we didn't write the correct number of bytes, mark the queue as
	 * failed.  Note that since datalen can't be equal to (size_t)(-1)
	 * this also handles the case of writelen == -1.
	 */
	if ((size_t)(writelen) != WB->datalen)
		W->failed = 1;

	/* Free this buffer. */
	free(WB->buf);
	free(WB);

	/* If we failed, invoke the failure callback. */
	if (W->failed)
		return ((W->fail_callback)(W->fail_cookie));

	/* Poke the queue to launch more writes. */
	return (poke(W));
}

/* Poke the queue. */
static int
poke(struct netbuf_write * W)
{
	struct writebuf * WB;

	/* If a write is in progress or we have nothing to write, return. */
	if ((W->write_cookie != NULL) || (W->head == NULL))
		return (0);

	/* Sanity-check: We don't have a buffer in progress. */
	assert(W->curr == NULL);

	/* Sanity-check: If we've failed, we shouldn't get here. */
	assert(W->failed == 0);

	/* Start writing a buffer. */
	WB = W->head;
	if ((W->write_cookie = network_write(W->s, WB->buf,
	    WB->datalen, WB->datalen, writbuf, W)) == NULL)
		goto err0;

	/* Remove the buffer from the queue. */
	W->curr = WB;
	W->head = WB->next;
	if (W->head == NULL)
		W->tail = NULL;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Dummy callback for failure-reporting. */
static int
dummyfail(void * cookie)
{

	(void)cookie; /* UNUSED */

	/* We don't want to hear about it. */
	return (0);
}

/**
 * netbuf_write_init(s, fail_callback, fail_cookie):
 * Create and return a buffered writer attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to write to said
 * socket except via the returned writer until netbuf_write_free is called.
 * to destroy the writer.  If a write fails, ${fail_callback} will be invoked
 * with the parameter ${fail_cookie}.
 */
struct netbuf_write *
netbuf_write_init(int s, int (* fail_callback)(void *), void * fail_cookie)
{
	struct netbuf_write * W;
	int val;

	/* Bake a cookie. */
	if ((W = malloc(sizeof(struct netbuf_write))) == NULL)
		goto err0;
	W->s = s;
	W->failed = 0;
	W->fail_callback = (fail_callback != NULL) ? fail_callback : dummyfail;
	W->fail_cookie = fail_cookie;
	W->head = W->tail = NULL;
	W->write_cookie = NULL;
	W->curr = NULL;

	/*
	 * Request that the OS not attempt to coalesce small segments.  We
	 * do this ourselves, and we're smarter than the OS is.  We don't
	 * check the error code here, because POSIX does not require that
	 * TCP_NODELAY be implemented (although it must be defined); and we
	 * might not even be operating on a TCP socket.
	 */
	val = 1;
	setsockopt(W->s, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(int));

	/* Success! */
	return (W);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * netbuf_write_write(W, buf, buflen):
 * Write ${buflen} bytes from the buffer ${buf} via the buffered writer ${W}.
 */
int
netbuf_write_write(struct netbuf_write * W, const uint8_t * buf, size_t buflen)
{
	struct writebuf * WB;

	/* If we've failed, just silently discard writes. */
	if (W->failed)
		return (0);

	/* Do we have a buffer with enough space? */
	if ((W->tail == NULL) ||
	    (W->tail->buflen - W->tail->datalen < buflen)) {
		/* We need to add a new buffer to the queue. */
		if ((WB = malloc(sizeof(struct writebuf))) == NULL)
			goto err0;

		/* We want to hold this write or WBUFLEN of small writes. */
		if (buflen > WBUFLEN)
			WB->buflen = buflen;
		else
			WB->buflen = WBUFLEN;

		/* Allocate memory. */
		if ((WB->buf = malloc(WB->buflen)) == NULL)
			goto err1;

		/* No data in this buffer yet. */
		WB->datalen = 0;

		/* Add this buffer to the queue. */
		if (W->tail == NULL)
			W->head = WB;
		else
			W->tail->next = WB;
		W->tail = WB;
		WB->next = NULL;
	}

	/* We should have a buffer with enough space now. */
	assert(W->tail != NULL);
	assert(W->tail->buflen - W->tail->datalen >= buflen);

	/* Copy the data in. */
	memcpy(&W->tail->buf[W->tail->datalen], buf, buflen);
	W->tail->datalen += buflen;

	/* Poke the queue to see if we can launch any more writes. */
	return (poke(W));

err1:
	free(WB);
err0:
	/* Failure! */
	return (-1);
}

/**
 * netbuf_write_free(W):
 * Free the writer ${W}.
 */
void
netbuf_write_free(struct netbuf_write * W)
{
	struct writebuf * WB;

	/* Cancel any in-progress write. */
	if (W->write_cookie != NULL) {
		network_write_cancel(W->write_cookie);
		free(W->curr->buf);
		free(W->curr);
	}

	/* Free write buffers. */
	while ((WB = W->head) != NULL) {
		W->head = WB->next;
		free(WB->buf);
		free(WB);
	}

	/* Free the buffered writer. */
	free(W);
}

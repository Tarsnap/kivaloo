#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "network.h"
#include "queue.h"

#include "netbuf.h"
#include "netbuf_ssl_internal.h"

#define WBUFLEN	4096

/*
 * Set to NULL here; initialized by netbuf_ssl if SSL is being used.  This
 * allows us to avoid needing to link libssl into binaries which aren't
 * going to be using SSL.
 */
void * (* netbuf_write_ssl_func)(struct network_ssl_ctx *, const uint8_t *,
    size_t, size_t, int (*)(void *, ssize_t), void *) = NULL;
void (* netbuf_write_ssl_cancel_func)(void *) = NULL;

/* Linked list of write buffers. */
struct writebuf {
	uint8_t * buf;			/* The buffer to be written. */
	size_t buflen;			/* Size of buffer. */
	size_t datalen;			/* Amount of data in buffer. */
	STAILQ_ENTRY(writebuf) entries;
};

/* Buffered writer structure. */
struct netbuf_write {
	int s;				/* Destination for writes... */
	struct network_ssl_ctx * ssl;	/* ... unless we're using this. */
	int reserved;			/* Some buffer space is reserved. */

	/* Failure handling. */
	int failed;			/* Has a write ever failed? */
	int (* fail_callback)(void *);	/* Callback to invoke on failure. */
	void * fail_cookie;		/* Cookie for failure callback. */

	/* Queued buffers. */
	STAILQ_HEAD(buffers, writebuf) buffers;	/* Queue of buffers to write. */

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

	/* Sanity-check: No callbacks while buffer space reserved. */
	assert(W->reserved == 0);

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
	if ((W->write_cookie != NULL) || (STAILQ_EMPTY(&W->buffers)))
		return (0);

	/* If we've failed, don't try to do anything more. */
	if (W->failed)
		return (0);

	/* Sanity-check: We don't have a buffer in progress. */
	assert(W->curr == NULL);

	/* Start writing a buffer. */
	WB = STAILQ_FIRST(&W->buffers);
	if (W->ssl) {
		if ((W->write_cookie = (netbuf_write_ssl_func)(W->ssl,
		    WB->buf, WB->datalen, WB->datalen, writbuf, W)) == NULL)
			goto err0;
	} else {
		if ((W->write_cookie = network_write(W->s, WB->buf,
		    WB->datalen, WB->datalen, writbuf, W)) == NULL)
			goto err0;
	}
	W->curr = WB;

	/* Remove the buffer from the queue. */
	STAILQ_REMOVE(&W->buffers, WB, writebuf, entries);

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
 * socket except via the returned writer until netbuf_write_free() is called.
 * If a write fails, ${fail_callback} will be invoked with the parameter
 * ${fail_cookie}.
 */
struct netbuf_write *
netbuf_write_init(int s, int (* fail_callback)(void *), void * fail_cookie)
{

	return (netbuf_write_init2(s, NULL, fail_callback, fail_cookie));
}

/**
 * netbuf_write_init2(s, ssl, fail_callback, fail_cookie):
 * Behave like netbuf_write_init() if ${ssl} is NULL.  If the SSL context ${ssl}
 * is not NULL, use it and ignore ${s}.
 */
struct netbuf_write *
netbuf_write_init2(int s, struct network_ssl_ctx * ssl,
    int (* fail_callback)(void *), void * fail_cookie)
{
	struct netbuf_write * W;
	int val;

	/* Bake a cookie. */
	if ((W = malloc(sizeof(struct netbuf_write))) == NULL)
		goto err0;
	W->s = s;
	W->ssl = ssl;
	W->reserved = 0;
	W->failed = 0;
	W->fail_callback = (fail_callback != NULL) ? fail_callback : dummyfail;
	W->fail_cookie = fail_cookie;
	STAILQ_INIT(&W->buffers);
	W->write_cookie = NULL;
	W->curr = NULL;

	/* Additional configuration if we're using a socket. */
	if (W->ssl == NULL) {
		/* Sanity check. */
		assert(W->s >= 0);

		/*
		 * Request that the OS not attempt to coalesce small segments.
		 * We do this ourselves, and we're smarter than the OS is.  We
		 * don't check the error code here, because POSIX does not
		 * require that TCP_NODELAY be implemented (although it must
		 * be defined); and we might not even be operating on a TCP
		 * socket.
		 */
		val = 1;
		(void)setsockopt(W->s, IPPROTO_TCP, TCP_NODELAY, &val,
		    sizeof(int));
	}

	/* Success! */
	return (W);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * netbuf_write_reserve(W, len):
 * Reserve ${len} bytes of space in the buffered writer ${W} and return a
 * pointer to the buffer.  This operation must be followed by a call to
 * netbuf_write_consume() before the next call to _reserve() or _write() and
 * before a callback could be made into netbuf_write() (i.e., before control
 * returns to the event loop).
 */
uint8_t *
netbuf_write_reserve(struct netbuf_write * W, size_t len)
{
	struct writebuf * WB;

	/* Sanity-check: No calls while buffer space reserved. */
	assert(W->reserved == 0);

	/* We're reserving some space. */
	W->reserved = 1;

	/* Do we have a buffer with enough space?  Return it. */
	if ((WB = STAILQ_LAST(&W->buffers, writebuf, entries)) != NULL) {
		if (WB->buflen - WB->datalen >= len)
			goto oldbuf;
	}

	/* We need to add a new buffer to the queue. */
	if ((WB = malloc(sizeof(struct writebuf))) == NULL)
		goto err0;

	/* We want to hold this write or WBUFLEN of small writes. */
	if (len > WBUFLEN)
		WB->buflen = len;
	else
		WB->buflen = WBUFLEN;

	/* Allocate memory. */
	if ((WB->buf = malloc(WB->buflen)) == NULL)
		goto err1;

	/* No data in this buffer yet. */
	WB->datalen = 0;

	/* Add this buffer to the queue. */
	STAILQ_INSERT_TAIL(&W->buffers, WB, entries);

	/* Return a pointer to the new buffer. */
	return (WB->buf);

oldbuf:
	/* Return a pointer into the old buffer. */
	return (&WB->buf[WB->datalen]);

err1:
	free(WB);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * netbuf_write_consume(W, len):
 * Consume a reservation previously made by netbuf_write_reserve(); the value
 * ${len} must be <= the value passed to netbuf_write_reserve().
 */
int
netbuf_write_consume(struct netbuf_write * W, size_t len)
{
	struct writebuf * WB;

	/* Sanity-check: We must have space reserved. */
	assert(W->reserved == 1);

	/* Get the old buffer. */
	WB = STAILQ_LAST(&W->buffers, writebuf, entries);

	/* Sanity-check: We must have a buffer. */
	assert(WB != NULL);

	/* Sanity-check: We must have enough space reserved. */
	assert(WB->buflen - WB->datalen >= len);

	/*
	 * Advance the buffer pointer, unless we've failed -- if we've failed
	 * there's no point since we're never going to look at the buffered
	 * data anyway.
	 */
	if (W->failed == 0)
		WB->datalen += len;

	/* We no longer have space reserved. */
	W->reserved = 0;

	/* Poke the queue to see if we can launch more writing now. */
	return (poke(W));
}

/**
 * netbuf_write_write(W, buf, buflen):
 * Write ${buflen} bytes from the buffer ${buf} via the buffered writer ${W}.
 */
int
netbuf_write_write(struct netbuf_write * W, const uint8_t * buf, size_t buflen)
{
	uint8_t * wbuf;

	/* If we've failed, just silently discard writes. */
	if (W->failed)
		return (0);

	/* Reserve space to write the data into. */
	if ((wbuf = netbuf_write_reserve(W, buflen)) == NULL)
		goto err0;

	/* Copy data into the returned buffer. */
	memcpy(wbuf, buf, buflen);

	/* Consume the reservation. */
	if (netbuf_write_consume(W, buflen))
		goto err0;

	/* Success! */
	return (0);

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

        /* Behave consistently with free(NULL). */
	if (W == NULL)
		return;

	/* Cancel any in-progress write. */
	if (W->write_cookie != NULL) {
		if (W->ssl)
			(netbuf_write_ssl_cancel_func)(W->write_cookie);
		else
			network_write_cancel(W->write_cookie);
		free(W->curr->buf);
		free(W->curr);
	}

	/* Free write buffers. */
	while (!STAILQ_EMPTY(&W->buffers)) {
		WB = STAILQ_FIRST(&W->buffers);
		STAILQ_REMOVE_HEAD(&W->buffers, entries);
		free(WB->buf);
		free(WB);
	}

	/* Free the buffered writer. */
	free(W);
}

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "mpool.h"
#include "network.h"
#include "warnp.h"

#include "netbuf.h"

/* Single write structure. */
struct write_cookie {
	int (* callback)(void *, int);
	void * cookie;
	const uint8_t * buf;
	size_t buflen;
	struct write_cookie * next;
};

/* Buffered writer structure. */
struct netbuf_write {
	int s;				/* Destination for writes. */
	uint8_t * buf;			/* Write aggregation buffer. */
	size_t buflen;			/* Write aggregation buffer length. */
	size_t nwrites;			/* # writes currently aggregated. */
	struct write_cookie * head;	/* Write queue head. */
	struct write_cookie ** tail;	/* Pointer to terminating NULL. */
	struct write_cookie * head_ip;	/* Queue of in-progress writes. */
	void * write_cookie;		/* Cookie from network_write. */
	size_t writelen;		/* Length being written. */
	int failed;			/* Has a write ever failed? */
	int destroyed;			/* Has _destroy been called? */
};

MPOOL(cookies, struct write_cookie, 4096 * 4);

static int dofailure(void *);
static int writbuf(void *, ssize_t);
static int poke(struct netbuf_write *);

/* Perform a failure callback for the specified buffer, then clean up. */
static int
dofailure(void * cookie)
{
	struct write_cookie * WC = cookie;
	int rc;

	/* Notify upstream. */
	rc = (WC->callback)(WC->cookie, 1);

	/* Free the cookie. */
	mpool_cookies_free(WC);

	/* Return status from callback. */
	return (rc);
}

/* A buffer has been written. */
static int
writbuf(void * cookie, ssize_t writelen)
{
	struct netbuf_write * W = cookie;
	struct write_cookie * WC, * head;
	size_t i, nwrites;
	int rc;

	/* Sanity-check: We must have had a write in progress. */
	assert(W->write_cookie != NULL);
	assert(W->head_ip != NULL);

	/* This write is no longer in progress. */
	W->write_cookie = NULL;

	/*
	 * If we didn't write the correct number of bytes, mark the queue as
	 * failed.  Note that since W->writelen can't be equal to (size_t)(-1)
	 * this also handles the case of writelen == -1.
	 */
	if ((size_t)(writelen) != W->writelen)
		W->failed = 1;

	/* Dequeue buffers and perform callbacks. */
	nwrites = W->nwrites;
	head = W->head_ip;
	for (i = 0; i < nwrites; i++) {
		/* Grab a callback from the linked list. */
		WC = head;
		head = WC->next;

		/* Notify upstream. */
		rc = (WC->callback)(WC->cookie, W->failed);

		/* Free the cookie. */
		mpool_cookies_free(WC);

		/* If the callback failed, error out. */
		if (rc)
			goto err0;
	}

	/* Poke the writer to handle more buffers. */
	if (poke(W))
		goto err1;

	/* Success! */
	return (0);

err1:
	rc = -1;
err0:
	/* Failure! */
	return (rc);
}

/* Poke the queue. */
static int
poke(struct netbuf_write * W)
{
	struct write_cookie * WC;
	size_t writelen;
	const uint8_t * buf;

	/* If there is a write in progress, do nothing. */
	if (W->write_cookie != NULL)
		goto done;

	/* If the writer has failed or been destroyed, schedule failures. */
	if (W->failed) {
		while (W->head != NULL) {
			/* Remove from the queue. */
			WC = W->head;
			W->head = WC->next;

			/* Schedule failure callback. */
			if (!events_immediate_register(dofailure, WC, 0))
				goto err0;
		}
	}

	/* If we have nothing to write, we're done. */
	if (W->head == NULL)
		goto done;

	/*
	 * If we have only one write available or we can't fit 2 writes into
	 * the aggregation buffer, just do a direct write.
	 */
	if ((W->head->next == NULL) ||
	    (W->head->buflen + W->head->next->buflen > W->buflen)) {
		W->head_ip = W->head;
		W->nwrites = 1;
		buf = W->head->buf;
		W->writelen = W->head->buflen;
		W->head = W->head->next;
	} else {
		writelen = 0;
		W->head_ip = W->head;
		for (W->nwrites = 0; W->head != NULL; W->nwrites++) {
			WC = W->head;
			if (writelen + WC->buflen > W->buflen)
				break;
			memcpy(&W->buf[writelen], WC->buf, WC->buflen);
			writelen += WC->buflen;
			W->head = WC->next;
		}
		buf = W->buf;
		W->writelen = writelen;
	}

	/* Launch the write. */
	if ((W->write_cookie = network_write(W->s,
	    buf, W->writelen, W->writelen, writbuf, W)) == NULL)
		goto err0;

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * netbuf_write_init(s):
 * Create and return a buffered writer attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to write to said
 * socket except via the returned writer until netbuf_write_destroy is called
 * to destroy the writer.
 */
struct netbuf_write *
netbuf_write_init(int s)
{
	struct netbuf_write * W;
	int val;

	/* Bake a cookie. */
	if ((W = malloc(sizeof(struct netbuf_write))) == NULL)
		goto err0;
	W->s = s;
	W->buflen = 4096;
	W->head = W->head_ip = NULL;
	W->write_cookie = NULL;
	W->failed = 0;
	W->destroyed = 0;

	/*
	 * Request that the OS not attempt to coalesce small segments.  We
	 * do this ourselves, and we're smarter than the OS is.  We don't
	 * check the error code here, because POSIX does not require that
	 * TCP_NODELAY be implemented (although it must be defined); and we
	 * might not even be operating on a TCP socket.
	 */
	val = 1;
	setsockopt(W->s, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(int));

	/* Allocate an aggregation buffer. */
	if ((W->buf = malloc(W->buflen)) == NULL)
		goto err1;

	/* Success! */
	return (W);

err1:
	free(W);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * netbuf_write_write(W, buf, buflen, callback, cookie):
 * Write ${buflen} bytes from the buffer ${buf} via the buffered writer ${W}.
 * Invoke ${callback}(${cookie}, status) when done, with status set to 0 on
 * success, and set to 1 on failure.  A call to netbuf_write_destroy can be
 * made from within the callback, but not a call to netbuf_write_free.
 */
int
netbuf_write_write(struct netbuf_write * W, const uint8_t * buf,
    size_t buflen, int (* callback)(void *, int), void * cookie)
{
	struct write_cookie * WC;

	/* Bake a cookie. */
	if ((WC = mpool_cookies_malloc()) == NULL)
		goto err0;
	WC->callback = callback;
	WC->cookie = cookie;
	WC->buf = buf;
	WC->buflen = buflen;
	WC->next = NULL;

	/* Add to the queue. */
	if (W->head == NULL)
		W->head = WC;
	else
		*(W->tail) = WC;
	W->tail = &WC->next;

	/* Poke the queue. */
	if (poke(W))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * netbuf_write_destroy(W):
 * Destroy the writer ${W}.  The write-completion callbacks will be queued to
 * be performed as failures after netbuf_write_destroy returns.  On error
 * return, the queue will be destroyed but some callbacks may be lost.
 */
int
netbuf_write_destroy(struct netbuf_write * W)
{
	struct write_cookie * WC;
	size_t i;

	/* Mark the queue as destroyed. */
	W->destroyed = 1;

	/* All further callbacks will be performed as failures. */
	W->failed = 1;

	/* If there is a write in progress... */
	if (W->write_cookie) {
		/* ... cancel the write... */
		network_write_cancel(W->write_cookie);
		W->write_cookie = NULL;

		/* ... and arrange failure callbacks for the write. */
		for (i = 0; i < W->nwrites; i++) {
			/* Grab the first callback from the queue. */
			WC = W->head_ip;
			W->head_ip = WC->next;

			/* Schedule failure callback. */
			if (!events_immediate_register(dofailure, WC, 0))
				goto err1;
		}
	}

	/* Poke the queue. */
	if (poke(W))
		goto err0;

	/* Success! */
	return (0);

err1:
	poke(W);
err0:
	/* Failure! */
	return (-1);
}

/**
 * netbuf_write_free(W):
 * Free the writer ${W}.  The writer must have been previously destroyed by a
 * call to netbuf_write_destroy.
 */
void
netbuf_write_free(struct netbuf_write * W)
{

	/* We must have been destroyed. */
	assert(W->destroyed);

	/* Free the write aggregation buffer. */
	free(W->buf);

	/* Free the cookie. */
	free(W);
}

#include "events.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "warnp.h"

#include "kvlds.h"

struct donecookie {
	int failed;
	int done;
};

static int
callback_done(void * cookie, int failed)
{
	struct donecookie * C = cookie;

	C->failed = failed;
	C->done = 1;

	return (0);
}

struct multisetcookie {
	struct wire_requestqueue * Q;
	int (* callback)(void *, struct kvldskey **, struct kvldskey **);
	void * cookie;
	size_t inflight;
	int eof;
	int failed;
	int done;
};

static int callback_multiset(void *, int);

static int
multiset_send(struct multisetcookie * C)
{
	struct kvldskey * key;
	struct kvldskey * value;

	/* If we've hit EOF, there's nothing to do. */
	if (C->eof)
		goto done;

	/* If requests are failing, there's nothing to do. */
	if (C->failed)
		goto done;

	/* Send requests as long as we don't have too many in flight. */
	while (C->inflight < 4096) {
		if ((C->callback)(C->cookie, &key, &value)) {
			/* Failing to get more key-value pairs. */
			C->failed = 1;
			goto done;
		}

		/* Are we at EOF? */
		if (key == NULL) {
			C->eof = 1;
			goto done;
		}

		/* Store this key-value pair. */
		if (proto_kvlds_request_set(C->Q, key, value,
		    callback_multiset, C)) {
			warnp("proto_kvlds_request_set");
			C->failed = 1;
			goto err0;
		}

		/* Another request is in flight. */
		C->inflight += 1;

		/* Free the key and value. */
		kvldskey_free(key);
		kvldskey_free(value);
	}

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static int
callback_multiset(void * cookie, int failed)
{
	struct multisetcookie * C = cookie;

	/* This request is no longer in flight. */
	C->inflight -= 1;

	/* Did we fail? */
	if (failed)
		C->failed = 1;

	/* Can we send more requests? */
	if (multiset_send(C))
		goto err0;

	/* Are we finished? */
	if (C->inflight == 0)
		C->done = 1;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * kvlds_multiset(Q, callback_pair, cookie):
 * Store a series of key-value pairs.  The function
 *     ${callback_pair}(${cookie}, key, value)
 * will be repeatedly invoked and key-value pairs returned from it will be
 * stored and then freed; returning with ${key} set to NULL indicates that
 * no further pairs are to be stored.  Return 0 on success; or nonzero if
 * a KVLDS request failed or any of the callbacks returned nonzero.
 *
 * This function may call events_run internally.
 */
int
kvlds_multiset(struct wire_requestqueue * Q,
    int (* callback_pair)(void *, struct kvldskey **, struct kvldskey **),
    void * cookie)
{
	struct multisetcookie C = {
		.Q = Q,
		.callback = callback_pair,
		.cookie = cookie,
		.inflight = 0,
		.eof = 0,
		.failed = 0,
		.done = 0
	};

	/* Start sending requests. */
	if (multiset_send(&C))
		goto err0;

	/* Wait until we've finished. */
	if (events_spin(&C.done)) {
		warnp("Error running event loop");
		goto err0;
	}

	/* Did we succeed? */
	return (C.failed);

err0:
	/* Failure! */
	return (-1);
}

struct rangecookie {
	int (* callback)(void *,
	    const struct kvldskey *, const struct kvldskey *);
	void * cookie;
	struct donecookie C;
};

static int
callback_range_item(void * cookie,
   const struct kvldskey * key, const struct kvldskey * value)
{
	struct rangecookie * C = cookie;

	return ((C->callback)(C->cookie, key, value));
}

static int
callback_range_done(void * cookie, int failed)
{
	struct rangecookie * C = cookie;

	return (callback_done(&C->C, failed));
}

/**
 * kvlds_range(Q, start, end, callback, cookie):
 * List key-value pairs satisfying ${start} <= key < ${end}.  Invoke
 *     ${callback}(${cookie}, key, value)
 * for each such key.  Return 0 on success; or nonzero if a KVLDS request
 * failed or any of the callbacks returned nonzero.
 *
 * This function may call events_run internally.
 */
int
kvlds_range(struct wire_requestqueue * Q, const struct kvldskey * start,
    const struct kvldskey * end, int (* callback)(void *,
        const struct kvldskey *, const struct kvldskey *),
    void * cookie)
{
	struct rangecookie C = {
		.callback = callback,
		.cookie = cookie,
		.C = { .done = 0 }
	};

	/* Start dumping key-value pairs. */
	if (proto_kvlds_request_range2(Q, start, end,
	    callback_range_item, callback_range_done, &C)) {
		warnp("proto_kvlds_request_range2");
		goto err0;
	}

	/* Wait until we've finished. */
	if (events_spin(&C.C.done)) {
		warnp("Error running event loop");
		goto err0;
	}

	/* Did we succeed? */
	return (C.C.failed);

err0:
	/* Failure! */
	return (-1);
}

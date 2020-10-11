#ifndef _KVLDS_H_
#define _KVLDS_H_

/* Opaque types. */
struct kvldskey;
struct wire_requestqueue;

/**
 * kvlds_multiset(Q, callback_pair, cookie):
 * Store a series of key-value pairs.  The function
 *     ${callback_pair}(${cookie}, key, value)
 * will be repeatedly invoked and key-value pairs returned from it will be
 * stored and then freed; returning with ${key} set to NULL indicates that
 * no further pairs are to be stored.  Return 0 on success; or nonzero if
 * a KVLDS request failed or any of the callbacks returned nonzero.
 *
 * This function may call events_run() internally.
 */
int kvlds_multiset(struct wire_requestqueue *,
    int (*)(void *, struct kvldskey **, struct kvldskey **),
    void *);

/**
 * kvlds_range(Q, start, end, callback, cookie):
 * List key-value pairs satisfying ${start} <= key < ${end}.  Invoke
 *     ${callback}(${cookie}, key, value)
 * for each such key.  Return 0 on success; or nonzero if a KVLDS request
 * failed or any of the callbacks returned nonzero.
 *
 * This function may call events_run() internally.
 */
int kvlds_range(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, const struct kvldskey *, const struct kvldskey *),
    void *);

/**
 * kvlds_set(Q, key, value):
 * Store a key-value pair.
 *
 * This function may call events_run() internally.
 */
int kvlds_set(struct wire_requestqueue *, const struct kvldskey *,
    const struct kvldskey *);

/**
 * kvlds_get(Q, key, value):
 * Get the value associated with ${key} and store it in ${value}.
 *
 * This function may call events_run() internally.
 */
int kvlds_get(struct wire_requestqueue *, const struct kvldskey *,
    struct kvldskey **);

/**
 * kvlds_delete(Q, key):
 * Delete the value associated with ${key}.
 *
 * This function may call events_run() internally.
 */
int kvlds_delete(struct wire_requestqueue *, const struct kvldskey *);

#endif /* !_KVLDS_H_ */

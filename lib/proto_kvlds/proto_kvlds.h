#ifndef _PROTO_KVLDS_H_
#define _PROTO_KVLDS_H_

/* Opaque types. */
struct kvldskey;
struct netbuf_read;
struct netbuf_write;
struct wire_requestqueue;

/**
 * proto_kvlds_request_params(Q, callback, cookie):
 * Send a PARAMS request to get the maximum key and value lengths via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, kmax, vmax)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * kmax is the maximum key length, and vmax is the maximum value length.
 */
int proto_kvlds_request_params(struct wire_requestqueue *,
    int (*)(void *, int, size_t, size_t), void *);

/**
 * proto_kvlds_request_set(Q, key, value, callback, cookie):
 * Send a SET request to associate the value ${value} with the key ${key} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int proto_kvlds_request_set(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, int), void *);

/**
 * proto_kvlds_request_cas(Q, key, oval, value, callback, cookie):
 * Send a CAS request to associate the value ${value} with the key ${key} iff
 * the current value is ${oval} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int proto_kvlds_request_cas(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, int, int), void *);

/**
 * proto_kvlds_request_add(Q, key, value, callback, cookie):
 * Send an ADD request to associate the value ${value} with the key ${key}
 * iff there is no current value set via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int proto_kvlds_request_add(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, int, int), void *);

/**
 * proto_kvlds_request_modify(Q, key, value, callback, cookie):
 * Send a MODIFY request to associate the value ${value} with the key ${key}
 * iff there is a current value set via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was set and 1 if it was not set.
 */
int proto_kvlds_request_modify(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, int, int), void *);

/**
 * proto_kvlds_request_delete(Q, key, callback, cookie):
 * Send a DELETE request to delete the value (if any) associated with the
 * key ${key} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int proto_kvlds_request_delete(struct wire_requestqueue *,
    const struct kvldskey *, int (*)(void *, int), void *);

/**
 * proto_kvlds_request_cad(Q, key, oval, callback, cookie):
 * Send a CAD request to delete the value associated with the key ${key}
 * iff it is currently ${oval} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and status is 0 if the value was deleted and 1 if it was not deleted.
 */
int proto_kvlds_request_cad(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, int, int), void *);

/**
 * proto_kvlds_request_get(Q, key, callback, cookie):
 * Send a GET request to read the value associated with the key ${key} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, value)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * and value is the value associated with the key ${key} or NULL if no value
 * associated.  The callback is responsible for freeing ${value}.
 */
int proto_kvlds_request_get(struct wire_requestqueue *,
    const struct kvldskey *,
    int (*)(void *, int, struct kvldskey *), void *);

/**
 * proto_kvlds_request_range(Q, start, end, max, callback, cookie):
 * Send a RANGE request to list key-value pairs which are >= ${start} and
 * < ${end} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, nkeys, next, keys, values)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * nkeys is the number of key-value pairs returned, and keys and values are
 * arrays of keys and values respectively.  The callback is responsible for
 * freeing the provided structures.
 */
int proto_kvlds_request_range(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *, size_t,
    int (*)(void *, int, size_t, struct kvldskey *,
	struct kvldskey **, struct kvldskey **),
    void *);

/**
 * proto_kvlds_request_range2(Q, start, end, callback_item, callback, cookie):
 * Repeatedly use proto_kvlds_request_range to issue RANGE requests via the
 * request queue ${Q}.  Invoke
 *     ${callback_item}(${cookie}, key, value)
 * for each key-value pair returned, and invoke
 *     ${callback}(${cookie}, failed)
 * when all key-value pairs in the specified range have been handled.
 */
int proto_kvlds_request_range2(struct wire_requestqueue *,
    const struct kvldskey *, const struct kvldskey *,
    int (*)(void *, const struct kvldskey *, const struct kvldskey *),
    int (*)(void *, int), void *);

/* Packet types. */
#define PROTO_KVLDS_PARAMS	0x00000100
#define PROTO_KVLDS_SET		0x00000110
#define PROTO_KVLDS_CAS		0x00000111
#define PROTO_KVLDS_ADD		0x00000112
#define PROTO_KVLDS_MODIFY	0x00000113
#define PROTO_KVLDS_DELETE	0x00000120
#define PROTO_KVLDS_CAD		0x00000121
#define PROTO_KVLDS_GET		0x00000130
#define PROTO_KVLDS_RANGE	0x00000131
#define PROTO_KVLDS_NONE	(uint32_t)(-1)

/* KVLDS request structure. */
struct proto_kvlds_request {
	uint64_t ID;
	uint32_t type;
	uint32_t range_max;
	const struct kvldskey * key;
#define range_start key
	const struct kvldskey * value;
#define range_end value
	const struct kvldskey * oval;
	uint8_t blob[4 + 3 * 256];
};

/**
 * proto_kvlds_request_alloc():
 * Allocate a struct proto_kvlds_request.
 */
struct proto_kvlds_request * proto_kvlds_request_alloc(void);

/**
 * proto_kvlds_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an KVLDS request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_KVLDS_NONE.
 */
int proto_kvlds_request_read(struct netbuf_read *,
    struct proto_kvlds_request *);

/**
 * proto_kvlds_request_free(req):
 * Free the struct proto_kvlds_request ${req}.
 */
void proto_kvlds_request_free(struct proto_kvlds_request *);

/**
 * proto_kvlds_response_params(Q, ID, kmax, vmax):
 * Send a PARAMS response with ID ${ID} specifying that the maximum key
 * length is ${kmax} bytes and the maximum value length is ${vmax} bytes
 * to the write queue ${Q}.
 */
int proto_kvlds_response_params(struct netbuf_write *, uint64_t, uint32_t,
    uint32_t);

/**
 * proto_kvlds_response_status(Q, ID, status):
 * Send a SET/CAS/ADD/MODIFY/DELETE/CAD response with ID ${ID} and status
 * ${status} to the write queue ${Q} indicating that the request has been
 * completed with the specified status.
 */
int proto_kvlds_response_status(struct netbuf_write *, uint64_t, uint32_t);

#define proto_kvlds_response_set(Q, ID)		\
	proto_kvlds_response_status(Q, ID, 0)
#define proto_kvlds_response_cas(Q, ID, status)	\
	proto_kvlds_response_status(Q, ID, status)
#define proto_kvlds_response_add(Q, ID, status)	\
	proto_kvlds_response_status(Q, ID, status)
#define proto_kvlds_response_modify(Q, ID, status)	\
	proto_kvlds_response_status(Q, ID, status)
#define proto_kvlds_response_delete(Q, ID)		\
	proto_kvlds_response_status(Q, ID, 0)
#define proto_kvlds_response_cad(Q, ID, status)	\
	proto_kvlds_response_status(Q, ID, status)

/**
 * proto_kvlds_response_get(Q, ID, status, value):
 * Send a GET response with ID ${ID}, status ${status}, and value ${value}
 * (if ${status} == 0) to the write queue ${Q} indicating that the provided
 * key is associated with the specified data (or not).
 */
int proto_kvlds_response_get(struct netbuf_write *, uint64_t, uint32_t,
    const struct kvldskey *);

/**
 * proto_kvlds_response_range(Q, ID, nkeys, next, keys, values):
 * Send a RANGE response with ID ${ID}, next key ${next} and ${nkeys}
 * key-value pairs with the keys in ${keys} and values in ${values} to the
 * write queue ${Q}.
 */
int proto_kvlds_response_range(struct netbuf_write *, uint64_t, size_t,
    const struct kvldskey *, struct kvldskey **, struct kvldskey **);

#endif /* !_PROTO_LBS_H_ */

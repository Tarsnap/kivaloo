#ifndef _PROTO_DYNAMODB_KV_H_
#define _PROTO_DYNAMODB_KV_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct netbuf_read;
struct netbuf_write;
struct wire_requestqueue;

/**
 * proto_dynamodb_kv_request_put(Q, key, buf, buflen, callback, cookie):
 * Send a request to associate the value ${buf} (of length ${buflen}) with
 * the key ${key} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status)
 * upon request completion, where ${status} is 0 on success and 1 on failure.
 * The value must be of length at most 256 kiB.
 */
int proto_dynamodb_kv_request_put(struct wire_requestqueue *, const char *,
    const uint8_t *, size_t, int (*)(void *, int), void *);

/**
 * proto_dynamodb_kv_request_get(Q, key, callback, cookie):
 * Send a request to read the value associated with the key ${key} via the
 * request queue ${Q}.  The value must be of length at most ${maxlen}.
 * Invoke
 *     ${callback}(${cookie}, status, buf, len)
 * upon request completion, where ${status} is 0 on success, 1 on failure,
 * and 2 if there is no such key/value pair; and (on success) ${len} is the
 * length of the value returned via ${buf}.
 */
int proto_dynamodb_kv_request_get(struct wire_requestqueue *, const char *,
    int (*)(void *, int, const uint8_t *, size_t), void *);

/**
 * proto_dynamodb_kv_request_getc(Q, key, callback, cookie):
 * As proto_dynamodb_kv_request_get(), except that the underlying DynamoDB
 * request is made with strong consistency.
 */
int proto_dynamodb_kv_request_getc(struct wire_requestqueue *, const char *,
    int (*)(void *, int, const uint8_t *, size_t), void *);

/**
 * proto_dynamodb_kv_request_delete(Q, key, callback, cookie):
 * Send a request to delete the key ${key} and its associated value via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status)
 * upon request completion, where ${status} is 0 on success and 1 on failure.
 */
int proto_dynamodb_kv_request_delete(struct wire_requestqueue *, const char *,
    int (*)(void *, int), void *);

/* Packet types. */
#define PROTO_DDBKV_PUT		0x00010100
#define PROTO_DDBKV_GET		0x00010110
#define PROTO_DDBKV_GETC	0x00010111
#define PROTO_DDBKV_DELETE	0x00010200
#define PROTO_DDBKV_NONE	((uint32_t)(-1))

/* DynamoDB-KV request structure. */
struct proto_ddbkv_request {
	/* Present for all requests. */
	uint64_t ID;
	uint32_t type;
	char * key;

	/* Present for PUT requests only. */
	uint32_t len;
	uint8_t * buf;
};

/**
 * proto_dynamodb_kv_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as a DynamoDB-KV request.
 * Return the parsed request via ${req}.  If no request is available, return
 * with ${req}->type == PROTO_DDBKV_NONE.
 */
int proto_dynamodb_kv_request_read(struct netbuf_read *,
    struct proto_ddbkv_request *);

/**
 * proto_dynamodb_kv_request_free(req):
 * Free the contents of the DynamoDB-KV request structure ${req}.  The
 * structure itself is not freed.
 */
void proto_dynamodb_kv_request_free(struct proto_ddbkv_request *);

/**
 * proto_dynamodb_kv_response_status(Q, ID, status):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the DynamoDB request completed successfully (${status} = 0) or failed
 * (${status} = 1).
 */
int proto_dynamodb_kv_response_status(struct netbuf_write *, uint64_t, int);

/* Convenience functions. */
#define proto_dynamodb_kv_response_put(Q, ID, status)		\
	proto_dynamodb_kv_response_status(Q, ID, status)
#define proto_dynamodb_kv_response_delete(Q, ID, status)	\
	proto_dynamodb_kv_response_status(Q, ID, status)

/**
 * proto_dynamodb_kv_response_data(Q, ID, status, len, buf):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the DynamoDB request completed successfully (${status} = 0) with the
 * provided data, failed (${status} = 1), or returned no data (${status} = 2).
 */
int proto_dynamodb_kv_response_data(struct netbuf_write *, uint64_t, int,
    uint32_t, const uint8_t *);

/* Convenience functions. */
#define proto_dynamodb_kv_response_get(Q, ID, status, len, buf)		\
	proto_dynamodb_kv_response_data(Q, ID, status, len, buf)
#define proto_dynamodb_kv_response_getc(Q, ID, status, len, buf)	\
	proto_dynamodb_kv_response_data(Q, ID, status, len, buf)

#endif /* !_PROTO_DYNAMODB_KV_H_ */

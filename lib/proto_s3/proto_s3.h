#ifndef _PROTO_S3_H_
#define _PROTO_S3_H_

#include <stdint.h>

/* Opaque types. */
struct netbuf_read;
struct netbuf_write;
struct wire_requestqueue;

/* Maximum size of S3 objects accessed via this interface. */
#define PROTO_S3_MAXLEN 0x80000000

/**
 * proto_s3_request_put(Q, bucket, object, buflen, buf, callback, cookie):
 * Send a PUT request to store ${buflen} bytes from ${buf} to the object
 * ${object} in the S3 bucket ${bucket} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where ${failed} is 0 on success and 1 on failure.
 */
int proto_s3_request_put(struct wire_requestqueue *, const char *,
    const char *, size_t, const uint8_t *, int (*)(void *, int), void *);

/**
 * proto_s3_request_get(Q, bucket, object, maxlen, callback, cookie):
 * Send a GET request to read up to ${maxlen} bytes from the object ${object}
 * in the S3 bucket ${bucket} via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, len, buf)
 * upon request completion, where ${failed} is 0 on success and 1 on failure,
 * ${len} is the length of the object (up to ${maxlen}) or -1 (on failure or
 * if the object is larger than ${maxlen} bytes), and ${buf} contains the
 * object data (if ${len} != -1) or is NULL (if ${len} == -1).
 */
int proto_s3_request_get(struct wire_requestqueue *, const char *,
    const char *, size_t,
    int (*)(void *, int, size_t, const uint8_t *), void *);

/**
 * proto_s3_request_range(Q, bucket, object, offset, len, callback, cookie):
 * Send a RANGE request to read ${len} bytes starting at offset ${offset} from
 * the object ${object} in the S3 bucket ${bucket} via the request queue ${Q}.
 * Invoke
 *     ${callback}(${cookie}, failed, buflen, buf)
 * upon request completion, where ${failed} is 0 on success or 1 on failure,
 * and ${buf} contains ${buflen} bytes of object data if ${failed} == 0 (note
 * that ${buflen} can be less than ${len} if the object contains fewer than
 * ${offset}+${len} bytes).
 */
int proto_s3_request_range(struct wire_requestqueue *, const char *,
    const char *, uint32_t, uint32_t,
    int (*)(void *, int, size_t, const uint8_t *), void *);

/**
 * proto_s3_request_head(Q, bucket, object, callback, cookie):
 * Send a HEAD request for the object ${object} in the S3 bucket ${bucket} via
 * the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, status, len)
 * upon request completion, where ${status} is the HTTP status code (or 0 on
 * error) and ${len} is the object size (if status == 200) or -1 (otherwise).
 */
int proto_s3_request_head(struct wire_requestqueue *, const char *,
    const char *, int (*)(void *, int, size_t), void *);

/**
 * proto_s3_request_delete(Q, bucket, object, callback, cookie):
 * Send a DELETE request for the object ${object} in the S3 bucket ${bucket}
 * via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where ${failed} is 0 on success or 1 on failure.
 */
int proto_s3_request_delete(struct wire_requestqueue *, const char *,
    const char *, int (*)(void *, int), void *);

/* Packet types. */
#define PROTO_S3_PUT		0x00010000
#define PROTO_S3_GET		0x00010010
#define PROTO_S3_RANGE		0x00010011
#define PROTO_S3_HEAD		0x00010020
#define PROTO_S3_DELETE		0x00010030
#define PROTO_S3_NONE		((uint32_t)(-1))

/* S3 request structure. */
struct proto_s3_request {
	uint64_t ID;
	uint32_t type;
	char * bucket;
	char * object;
	union proto_s3_request_data {
		struct proto_s3_request_put {
			uint32_t len;		/* Object length. */
			uint8_t * buf;		/* Object data. */
		} put;
		struct proto_s3_request_get {
			uint32_t maxlen;	/* Maximum object size. */
		} get;
		struct proto_s3_request_range {
			uint32_t offset;	/* Position to start read. */
			uint32_t len;		/* Length to read. */
		} range;
		struct proto_s3_request_head {
			/* No parameters; dummy to avoid compiler warnings. */
			int dummy;		/* Dummy variable. */
		} head;
		struct proto_s3_request_delete {
			/* No parameters; dummy to avoid compiler warnings. */
			int dummy;		/* Dummy variable. */
		} delete;
	} r;
};

/**
 * proto_s3_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an S3 request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_S3_NONE.
 */
int proto_s3_request_read(struct netbuf_read *, struct proto_s3_request *);

/**
 * proto_s3_request_free(req):
 * Free the contents of the S3 request structure ${req}.
 */
void proto_s3_request_free(struct proto_s3_request *);

/**
 * proto_s3_response_status(Q, ID, status):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the S3 request completed with HTTP status code ${status}.
 */
int proto_s3_response_status(struct netbuf_write *, uint64_t, int);

#define proto_s3_response_put(Q, ID, status)		\
	proto_s3_response_status(Q, ID, status)
#define proto_s3_response_delete(Q, ID, status)		\
	proto_s3_response_status(Q, ID, status)

/**
 * proto_s3_response_data(Q, ID, status, len, buf):
 * Send a response with ID ${ID} to the write queue ${Q} indicating that
 * the S3 request completed with HTTP status code ${status} and the returned
 * data was ${len} bytes from ${buf}.  If ${buf} is NULL, send the length
 * ${len} but no data.
 */
int proto_s3_response_data(struct netbuf_write *, uint64_t, int,
    uint32_t, const uint8_t *);

#define proto_s3_response_get(Q, ID, status, len, buf)		\
	proto_s3_response_data(Q, ID, status, len, buf)
#define proto_s3_response_range(Q, ID, status, len, buf)	\
	proto_s3_response_data(Q, ID, status, len, buf)
#define proto_s3_response_head(Q, ID, status, len)		\
	proto_s3_response_data(Q, ID, status, len, NULL)

#endif /* !_PROTO_S3_H_ */

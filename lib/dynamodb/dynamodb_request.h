#ifndef _DYNAMODB_REQUEST_H_
#define _DYNAMODB_REQUEST_H_

#include <stdint.h>

/* Opaque types. */
struct http_response;
struct sock_addr;

/**
 * dynamodb_request(addrs, key_id, key_secret, region, op, body, bodylen,
 *     maxrlen, callback, cookie):
 * Using the AWS Key ID ${key_id} and Secret Access Key ${key_secret}, send
 * the DynamoDB request contained in ${body} (of length ${bodylen}) for the
 * operation ${op} to region ${region} located at ${addrs}.
 * 
 * Read a response with a body of up to ${maxrlen} bytes and invoke the
 * provided callback as ${callback}(${cookie}, ${response}), with a response
 * of NULL if no response was read (e.g., on connection error).  Return a
 * cookie which can be passed to http_request_cancel to abort the request.
 * (Note however that such a cancellation does not guarantee that the actual
 * DynamoDB operation will not occur and have results which are visible at a
 * later time.)
 * 
 * If the HTTP response has no body, the response structure will have bodylen
 * == 0 and body == NULL; if there is a body larger than ${maxrlen} bytes,
 * the response structure will have bodylen == (size_t)(-1) and body == NULL.
 * The callback is responsible for freeing the response body buffer (if any),
 * but not the rest of the response; it must copy any header strings before it
 * returns.  The provided request body buffer must remain valid until the
 * callback is invoked.
 */
void * dynamodb_request(struct sock_addr * const *, const char *,
    const char *, const char *, const char *, const uint8_t *, size_t,
    size_t, int (*)(void *, struct http_response *), void *);

#endif /* !_DYNAMODB_REQUEST_H_ */

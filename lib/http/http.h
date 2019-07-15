#ifndef _HTTP_H_
#define _HTTP_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque type. */
struct sock_addr;

struct http_header {
	const char * header;
	const char * value;
};

struct http_request {
	const char * method;
	const char * path;
	size_t nheaders;
	struct http_header * headers;
	size_t bodylen;
	const uint8_t * body;
};

struct http_response {
	int status;
	size_t nheaders;
	struct http_header * headers;
	size_t bodylen;
	uint8_t * body;
};

/**
 * http_request(addrs, request, maxrlen, callback, cookie):
 * Open a connection to ${addrs} and send the HTTP request ${request}.  Read a
 * response with a body of up to ${maxrlen} bytes and invoke the provided
 * callback as ${callback}(${cookie}, ${response}), with response == NULL if
 * no response was read (e.g., on connection error).  Return a cookie which can
 * be passed to http_request_cancel.
 *
 * If the response has no body, the response structure will have bodylen == 0
 * and body == NULL; if there is a body larger than ${maxrlen} bytes, the
 * response structure will have bodylen == (size_t)(-1) and body == NULL.
 * The callback is responsible for freeing the response body buffer (if any),
 * but not the rest of the response; it must copy any header strings before it
 * returns.  The provided request body buffer (if any) must remain valid until
 * the callback is invoked.
 */
void * http_request(struct sock_addr * const *, struct http_request *, size_t,
    int (*)(void *, struct http_response *), void *);

/**
 * https_request(addrs, request, maxrlen, callback, cookie, hostname):
 * Behave as http_request, but use HTTPS and verify that the target host is
 * ${hostname}.
 */
void * https_request(struct sock_addr * const *, struct http_request *,
    size_t, int (*)(void *, struct http_response *), void *, const char *);

/**
 * http_request_cancel(cookie):
 * Cancel the HTTP request for which ${cookie} was returned by http_request.
 * Do not invoke the associated callback function.
 */
void http_request_cancel(void *);

/**
 * http_findheader(headers, nheaders, header):
 * Search for ${header} in the ${nheaders} header structures ${headers}.
 * Return a pointer to the associated value or NULL if it is not found.
 */
const char * http_findheader(struct http_header *, size_t, const char *);

#endif /* !_HTTP_H_ */

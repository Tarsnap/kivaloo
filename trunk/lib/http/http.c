#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netbuf.h"
#include "network.h"
#include "sock.h"
#include "warnp.h"

#include "http.h"

/* We reject any response with more than 64 kB of headers. */
#define MAXHDR 65536

/* We expect a chunked transfer-encoding header to be at most 256 bytes. */
#define MAXCHLEN 256

/* HTTP request cookie. */
struct http_cookie {
	/* Connection parameters. */
	struct sock_addr * const * sas;	/* Addresses to attempt. */
	int s;				/* Network socket. */
	void * connect_cookie;		/* Cookie from network_connect. */
	struct netbuf_write * W;	/* Buffered writer. */
	struct netbuf_read * R;		/* Buffered reader. */

	/* Request parameters. */
	int req_ishead;			/* Is the method HEAD? */
	size_t req_headlen;		/* Length of req_head. */
	uint8_t * req_head;		/* Request header. */
	size_t req_bodylen;		/* Length of req_body. */
	const uint8_t * req_body;	/* Request body, if any. */

	/* Callback. */
	int (* callback)(void *, struct http_response *);
	void * cookie;

	/* Response-parsing state. */
	size_t hepos;			/* No \r\n\r\n before this point. */
	size_t res_headlen;		/* Length of res_head. */
	uint8_t * res_head;		/* Response header. */
	int chunked;			/* Using chunked Transfer-Encoding. */
	size_t readlen;			/* Length of current body read. */
	size_t res_bodylen_max;		/* Maximum response body length. */
	size_t res_bodylen_alloc;	/* Allocated length of res_body. */
	struct http_response res;	/* Response. */
};

static int callback_connected(void *, int);
static int callback_read_header(void *, int);
static int gotheaders(struct http_cookie *, uint8_t *, size_t);
static int callback_chunkedheader(void *, int);
static int get_body_gotclen(struct http_cookie *, size_t);
static int callback_read_toeof(void *, int);

/* Clean up a cookie and return -1. */
static int
die(struct http_cookie * H)
{

	/* Free the cookie. */
	http_request_cancel(H);

	/* Return failure status. */
	return (-1);
}

/* Perform a failure callback. */
static int
fail(void * cookie)
{
	struct http_cookie * H = cookie;
	int rc;

	/* Perform the callback. */
	rc = (H->callback)(H->cookie, NULL);

	/* Free the cookie. */
	http_request_cancel(H);

	/* Return status from callback. */
	return (rc);
}

/* Perform a success callback. */
static int
docallback(struct http_cookie * H)
{
	int rc;

	/* Perform callback. */
	rc = (H->callback)(H->cookie, &H->res);

	/* The callback function now owns the response body buffer. */
	H->res.body = NULL;

	/* Free the cookie. */
	http_request_cancel(H);

	/* Return status from callback. */
	return (rc);
}

/* Perform a body-is-too-large callback. */
static int
toobig(struct http_cookie * H)
{

	/* No body any more. */
	free(H->res.body);
	H->res.body = NULL;

	/* The response is too big. */
	H->res.bodylen = (size_t)(-1);

	/* Perform the callback. */
	return (docallback(H));
}

/* Find the first \r\n in the buffer, or return buflen. */
static size_t
findeol(uint8_t * buf, size_t buflen)
{
	size_t bufpos;

	/* Scan through the buffer. */
	for (bufpos = 0; bufpos + 2 <= buflen; bufpos++) {
		if (memcmp(&buf[bufpos], "\r\n", 2) == 0)
			return (bufpos);
	}

	/* No \r\n in buffer; return buflen. */
	return (buflen);
}

/* Grab and NUL-terminate a \r\n terminated line.  (Assert that one exists.) */
static uint8_t *
getline(uint8_t * buf, size_t buflen, size_t * bufpos, size_t * linelen)
{
	uint8_t * s = &buf[*bufpos];

	/* Find the \r\n. */
	*linelen = findeol(s, buflen - *bufpos);

	/* It had better be there. */
	assert(*linelen < buflen - *bufpos);

	/* Add NUL-terminator. */
	s[*linelen] = '\0';

	/* Adjust buffer position. */
	*bufpos += *linelen + 2;

	/* Return string. */
	return (s);
}

/* Add data to the body buffer. */
static int
addbody(struct http_cookie * H, uint8_t * buf, size_t buflen)
{
	size_t nalloc;
	uint8_t * nbuf;

	/* The caller should make sure we don't exceed the maximum length. */
	assert(H->res.bodylen + buflen <= H->res_bodylen_max);

	/* Reallocate if necessary. */
	if (H->res.bodylen + buflen > H->res_bodylen_alloc) {
		/* Double the buffer size. */
		nalloc = H->res_bodylen_alloc * 2;

		/* Increase if that isn't enough. */
		if (nalloc < H->res.bodylen + buflen)
			nalloc = H->res.bodylen + buflen;

		/* Decrease if we've gone too far. */
		if (nalloc > H->res_bodylen_max)
			nalloc = H->res_bodylen_max;

		/* Expand our memory allocation. */
		if ((nbuf = realloc(H->res.body, nalloc)) == NULL)
			goto err0;

		/* Record the new allocation and its size. */
		H->res.body = nbuf;
		H->res_bodylen_alloc = nalloc;
	}

	/* Copy the data into our (possibly expanded) buffer. */
	memcpy(&H->res.body[H->res.bodylen], buf, buflen);

	/* Record the increased data length. */
	H->res.bodylen += buflen;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

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
void *
http_request(struct sock_addr * const * addrs, struct http_request * request,
    size_t maxrlen, int (* callback)(void *, struct http_response *),
    void * cookie)
{
	struct http_cookie * H;
	char * s;
	size_t i;

	/* Bake a cookie. */
	if ((H = malloc(sizeof(struct http_cookie))) == NULL)
		goto err0;
	H->sas = addrs;
	H->s = -1;
	H->connect_cookie = NULL;
	H->W = NULL;
	H->R = NULL;
	H->callback = callback;
	H->cookie = cookie;
	H->hepos = 0;
	H->res_head = NULL;
	H->res_bodylen_max = maxrlen;
	H->res_bodylen_alloc = 0;
	H->res.status = 0;
	H->res.nheaders = 0;
	H->res.headers = NULL;
	H->res.bodylen = 0;
	H->res.body = NULL;

	/*
	 * Record whether this is a HEAD request; this matters when it comes
	 * to figuring out whether the response should have a body attached.
	 */
	if (strcmp(request->method, "HEAD") == 0)
		H->req_ishead = 1;
	else
		H->req_ishead = 0;

	/* Compute length of header. */
	H->req_headlen = strlen(request->method) + strlen(" ") +
	    strlen(request->path) + strlen(" HTTP/1.1\r\n");
	for (i = 0; i < request->nheaders; i++) {
		/* "header: value\r\n". */
		H->req_headlen += strlen(request->headers[i].header) +
		    strlen(request->headers[i].value) + 4;
	}
	H->req_headlen += 2;			/* Blank line. */

	/* Allocate space for header plus NUL byte (so we can use stpcpy). */
	if ((s = H->req_head = malloc(H->req_headlen + 1)) == NULL)
		goto err1;

	/* Construct request line. */
	s = stpcpy(s, request->method);
	s = stpcpy(s, " ");
	s = stpcpy(s, request->path);
	s = stpcpy(s, " HTTP/1.1\r\n");

	/* Append header lines. */
	for (i = 0; i < request->nheaders; i++) {
		s = stpcpy(s, request->headers[i].header);
		s = stpcpy(s, ": ");
		s = stpcpy(s, request->headers[i].value);
		s = stpcpy(s, "\r\n");
	}

	/* Append final blank line. */
	s = stpcpy(s, "\r\n");

	/* Sanity-check: We should have the right size of request header. */
	assert((uint8_t *)s == &H->req_head[H->req_headlen]);

	/* Record the request body parameters. */
	H->req_bodylen = request->bodylen;
	H->req_body = request->body;

	/* Connect to the target host. */
	if ((H->connect_cookie = network_connect(H->sas,
	    callback_connected, H)) == NULL)
		goto err2;

	/* Success! */
	return (H);

err2:
	free(H->req_head);
err1:
	free(H);
err0:
	/* Failure! */
	return (NULL);
}

/* We've connected to the target (or failed). */
static int
callback_connected(void * cookie, int s)
{
	struct http_cookie * H = cookie;

	/* We're not connecting any more. */
	H->connect_cookie = NULL;

	/* Did we fail? */
	if (s == -1)
		return (fail(H));

	/* We have a connected socket. */
	H->s = s;

	/* Create a reader and a writer. */
	if ((H->R = netbuf_read_init(H->s)) == NULL)
		return (die(H));
	if ((H->W = netbuf_write_init(H->s, fail, H)) == NULL)
		return (die(H));

	/* Send the request. */
	if (netbuf_write_write(H->W, H->req_head, H->req_headlen))
		return (die(H));
	if ((H->req_bodylen > 0) &&
	    netbuf_write_write(H->W, H->req_body, H->req_bodylen))
		return (die(H));

	/* Enter response-reading loop. */
	return (callback_read_header(H, 0));

	/* Success! */
	return (0);
}

/* Check if we have a complete header; or wait for more to arrive. */
static int
callback_read_header(void * cookie, int status)
{
	struct http_cookie * H = cookie;
	uint8_t * buf;
	size_t buflen;

	/* Did the read fail?  (EOF during headers counts as failing.) */
	if (status)
		return (fail(H));

	/* Where's the data? */
	netbuf_read_peek(H->R, &buf, &buflen);

	/* Scan forwards looking for \r\n\r\n. */
	for (; H->hepos + 4 <= buflen; H->hepos++) {
		if (memcmp(&buf[H->hepos], "\r\n\r\n", 4) == 0)
			break;
	}

	/* If we've found the end of the headers, handle them. */
	if (H->hepos + 4 <= buflen)
		return (gotheaders(H, buf, H->hepos + 4));

	/* Reject any response with more than 64 kB of headers. */
	if (buflen > MAXHDR) {
		warn0("Dropping connection with >%d bytes of headers", MAXHDR);
		return (fail(H));
	}

	/* Wait until at least one more byte has arrived. */
	if (netbuf_read_wait(H->R, buflen + 1, callback_read_header, H))
		return (die(H));

	/* Success! */
	return (0);
}

/* We have finished reading the request headers. */
static int
gotheaders(struct http_cookie * H, uint8_t * buf, size_t buflen)
{
	size_t bufpos;
	size_t linelen;
	size_t i;
	char * s;
	int major, minor;
	size_t len;
	const char * te;
	const char * clen;

	/* Suck the headers into a separate buffer. */
	H->res_headlen = buflen;
	if ((H->res_head = malloc(H->res_headlen)) == NULL)
		return (die(H));
	memcpy(H->res_head, buf, H->res_headlen);

	/* Consume the headers from the buffered reader. */
	netbuf_read_consume(H->R, H->res_headlen);

	/* Count header lines. */
	for (H->res.nheaders = 0, bufpos = 0;
	    bufpos < H->res_headlen;
	    H->res.nheaders++, bufpos += linelen + 2) {
		/* Find an EOL. */
		linelen = findeol(&H->res_head[bufpos],
		    H->res_headlen - bufpos);
	}

	/* # headers = # lines - 1 (status-line) - 1 (blank line). */
	H->res.nheaders -= 2;

	/* Allocate header structures. */
	if ((H->res.headers =
	    malloc(H->res.nheaders * sizeof(struct http_header))) == NULL)
		return (die(H));

	/* Go back to the start of the buffer for the parsing pass. */
	bufpos = 0;

	/* Find the status-line and check for premature NULs. */
	s = getline(H->res_head, H->res_headlen, &bufpos, &linelen);
	if (strlen(s) < linelen) {
		warn0("Status line contains NUL byte");
		return (fail(H));
	}

	/* Parse "HTTP/X.Y Z " from status-line and sanity-check. */
	if (sscanf(s, "HTTP/%d.%d %d ", &major, &minor, &H->res.status) < 3) {
		warn0("Invalid HTTP status-line: %s", s);
		return (fail(H));
	}
	if (major != 1) {
		warn0("HTTP response with major version > 1!");
		return (fail(H));
	}

	/* Parse headers. */
	for (i = 0; i < H->res.nheaders; i++) {
		/* Grab a line. */
		s = getline(H->res_head, H->res_headlen, &bufpos, &linelen);
		if (strlen(s) < linelen) {
			warn0("Header contains NUL byte");
			return (fail(H));
		}

		/* Split into header and value. */
		H->res.headers[i].header = strsep(&s, ":");
		H->res.headers[i].value = s;
	}

	/* We should be 2 bytes (\r\n) away from the end of the buffer. */
	assert(bufpos + 2 == H->res_headlen);

	/*
	 * If we received a 1xx response, we need to throw all the headers
	 * away and read a completely new response.  RFC 2616 says that a
	 * server can send a 1xx response whenever it likes and we must be
	 * prepared to accept it (but we may ignore it).
	 */
	if ((H->res.status >= 100) && (H->res.status <= 199)) {
		/* Free the headers. */
		free(H->res_head);
		free(H->res.headers);

		/* We don't want to try to free these again later. */
		H->res_head = NULL;
		H->res.headers = NULL;

		/* Go back to reading headers. */
		return (callback_read_header(H, 0));
	}

	/* If we don't expect any body, we can perform the callback now. */
	if ((H->req_ishead != 0) ||
	    (H->res.status == 204) || (H->res.status == 304)) {
		H->res.bodylen = 0;
		H->res.body = NULL;
		return (docallback(H));
	}

	/*
	 * If we have a "Transfer-Encoding: chunked" header, read the response
	 * body that way.
	 */
	if ((te = http_findheader(H->res.headers, H->res.nheaders,
	    "Transfer-Encoding")) != NULL) {
		if (strstr(te, "chunked") != NULL) {
			/* We're using chunked transfer-encoding. */
			H->chunked = 1;

			/* Read the first chunked header line. */
			return (callback_chunkedheader(H, 0));
		}
	}

	/*
	 * If we have a Content-Length header, parse the value; then read the
	 * specified number of bytes of body.
	 */
	if ((clen = http_findheader(H->res.headers, H->res.nheaders,
	    "Content-Length")) != NULL) {
		/* Parse the value (strtoull skips LWS). */
		len = strtoull(clen, NULL, 0);

		/* Read the body as a single blob. */
		return (get_body_gotclen(H, len));
	}

	/* Otherwise we need to just read until the connection is closed. */
	return (callback_read_toeof(H, 0));
}

/* Process arrived data, then read more data or chunk header, or callback. */
static int
callback_readdata(void * cookie, int status)
{
	struct http_cookie * H = cookie;
	uint8_t * buf;
	size_t buflen;
	size_t waitlen;

	/*
	 * Did we fail to read?  (EOF counts as a failure in this case, since
	 * we know exactly how many bytes of data should be arriving.)
	 */
	if (status)
		return (fail(H));

	/* What data has arrived? */
	netbuf_read_peek(H->R, &buf, &buflen);

	/* Don't bite off more than we can chew. */
	if (buflen > H->readlen)
		buflen = H->readlen;

	/* Add this to our internal buffer. */
	if (addbody(H, buf, buflen))
		return (die(H));

	/* Consume the data. */
	netbuf_read_consume(H->R, buflen);

	/* Adjust our remaining-read-length value. */
	H->readlen -= buflen;

	/* Are we done reading this block? */
	if (H->readlen == 0) {
		/* Was this just one chunk from a chunked encoding? */
		if (H->chunked) {
			/* Strip the trailing EOL. */
			H->res.bodylen -= 2;

			/* Get the next chunk. */
			return (callback_chunkedheader(H, 0));
		}

		/* If not, just do the callback. */
		return (docallback(H));
	}

	/*
	 * Wait for the MIN(remaining read length, 1 MB) to arrive.  This is
	 * a compromise between performance (larger reads have less overhead)
	 * and saving memory (if we're reading a large block, we don't want
	 * to buffer the whole thing twice).
	 */
	if (H->readlen > 1024 * 1024)
		waitlen = 1024 * 1024;
	else
		waitlen = H->readlen;

	/* Wait for more data to arrive. */
	if (netbuf_read_wait(H->R, waitlen, callback_readdata, H))
		return (die(H));

	/* Success! */
	return (0);
}

/* Read and parse a chunked header line. */
static int
callback_chunkedheader(void * cookie, int status)
{
	struct http_cookie * H = cookie;
	uint8_t * buf;
	size_t buflen;
	size_t eolpos;
	size_t clen;

	/* Did we fail?  (EOF while reading a chunk header is a failure.) */
	if (status)
		return (fail(H));

	/* Peek at the incoming data. */
	netbuf_read_peek(H->R, &buf, &buflen);

	/* Look for an EOL. */
	eolpos = findeol(buf, buflen);

	/* If we found one, handle the line. */
	if (eolpos != buflen) {
		/* Parse the chunk length. */
		clen = strtoull(buf, NULL, 16);

		/* Consume the line and EOL. */
		netbuf_read_consume(H->R, eolpos + 2);

		/* If this is zero, we're done! */
		if (clen == 0)
			return (docallback(H));

		/* Otherwise, check that it's not too big. */
		if (clen > H->res_bodylen_max - H->res.bodylen)
			return (toobig(H));
		if (clen > SIZE_MAX - 2)
			return (toobig(H));

		/* Read the chunk data plus extra EOL (we strip it later). */
		H->readlen = clen + 2;
		return (callback_readdata(H, 0));
	}

	/* If we've read MAXCHLEN bytes, we should have gotten an EOL. */
	if (buflen >= MAXCHLEN)
		return (fail(H));

	/* Wait until some more data arrives. */
	if (netbuf_read_wait(H->R, buflen + 1, callback_chunkedheader, H))
		return (die(H));

	/* Success! */
	return (0);
}

/* Read the response body based on the provided Content-Length. */
static int
get_body_gotclen(struct http_cookie * H, size_t len)
{

	/* Is the specified Content-Length too big? */
	if (len > H->res_bodylen_max)
		return (toobig(H));

	/* Record the length of content we need to read. */
	H->readlen = len;

	/* Once we've read this, we're done. */
	H->chunked = 0;

	/* Enter the reading loop. */
	return (callback_readdata(H, 0));
}

/* Read data until we hit EOF. */
static int
callback_read_toeof(void * cookie, int status)
{
	struct http_cookie * H = cookie;
	uint8_t * buf;
	size_t buflen;

	/* Did we fail? */
	if (status == -1)
		return (fail(H));

	/* Did we hit EOF? */
	if (status == 1)
		return (docallback(H));

	/* How much data is there? */
	netbuf_read_peek(H->R, &buf, &buflen);

	/* Is it too much? */
	if (buflen > H->res_bodylen_max - H->res.bodylen)
		return (toobig(H));

	/* Add this to our internal buffer. */
	if (addbody(H, buf, buflen))
		return (die(H));

	/* Consume the data. */
	netbuf_read_consume(H->R, buflen);

	/* Wait for at least one more byte to arrive. */
	if (netbuf_read_wait(H->R, 1, callback_read_toeof, H))
		return (die(H));

	/* Success! */
	return (0);
}

/**
 * http_request_cancel(cookie):
 * Cancel the HTTP request for which ${cookie} was returned by http_request.
 * Do not invoke the associated callback function.
 */
void
http_request_cancel(void * cookie)
{
	struct http_cookie * H = cookie;

	/* Stop connecting if we're in the process of doing so. */
	if (H->connect_cookie != NULL)
		network_connect_cancel(H->connect_cookie);

	/* If we have a network reader, cancel any in-progress read. */
	if (H->R != NULL)
		netbuf_read_wait_cancel(H->R);

	/* Free the network reader and writer if they exist. */
	if (H->W != NULL)
		netbuf_write_free(H->W);
	if (H->R != NULL)
		netbuf_read_free(H->R);

	/* Close the socket if we are connected. */
	if (H->s != -1)
		close(H->s);

	/*
	 * Free internal buffers.  (req_body does not need to be freed since
	 * it is owned by the caller; res_body does need to be freed, since
	 * it is set to NULL if/when it is passed off to the caller).
	 */
	free(H->req_head);
	free(H->res_head);
	free(H->res.headers);
	free(H->res.body);

	/* Free the cookie. */
	free(H);
}

/**
 * http_findheader(headers, nheaders, header):
 * Search for ${header} in the ${nheaders} header structures ${headers}.
 * Return a pointer to the associated value or NULL if it is not found.
 */
const char *
http_findheader(struct http_header * headers, size_t nheaders,
    const char * header)
{
	size_t i;

	/* Search for the header. */
	for (i = 0; i < nheaders; i++) {
		if (strcmp(headers[i].header, header) == 0)
			return (headers[i].value);
	}

	/* Didn't find it. */
	return (NULL);
}

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "b64encode.h"
#include "http.h"
#include "md5.h"
#include "sha1.h"
#include "sock.h"
#include "warnp.h"

#include "s3_request.h"

/**
 * s3_request(addrs, key_id, key_secret, request, maxrlen, callback, cookie):
 * Using the AWS Key ID ${key_id} and Secret Access Key ${key_secret}, send
 * the S3 request ${request}.  Behave identically to http_request otherwise.
 */
void *
s3_request(struct sock_addr * const * addrs, const char * key_id,
    const char * key_secret, struct s3_request * request, size_t maxrlen,
    int (* callback)(void *, struct http_response *), void * cookie)
{
	struct http_request RH;
	char date[80];
	uint8_t body_md5[16];
	char content_md5[25];
	char content_length[sizeof(size_t) * 3 + 1];
	uint8_t authsha1[20];
	char authsha1_64[29];
	const char * content_type = "";
	char * s;
	char * authorization;
	char * host;
	void * http_cookie;
	time_t t_now;

	/* Construct a Date header value. */
	if (time(&t_now) == (time_t)(-1)) {
		warnp("time");
		goto err0;
	}
	if (strftime(date, 80,
	    "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t_now)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* If we have a message body, construct Content-{MD5, Length}. */
	if (request->body) {
		/* Compute the MD5 of the request body and base64-encode. */
		MD5_Buf(request->body, request->bodylen, body_md5);
		b64encode(body_md5, content_md5, 16);

		/* Construct content length string. */
		sprintf(content_length, "%zu", request->bodylen);
	}

	/* Construct stringtosign. */
	if (asprintf(&s, "%s\n%s\n%s\n%s\n/%s%s",
	    request->method, (request->body != NULL) ? content_md5 : "",
	    content_type, date, request->bucket, request->path) == -1)
		goto err0;

	/* Compute HMAC-SHA1 and base64-encode. */
	HMAC_SHA1_Buf(key_secret, strlen(key_secret), s, strlen(s), authsha1);
	b64encode(authsha1, authsha1_64, 20);

	/* Free stringtosign. */
	free(s);

	/* Construct Authorization header. */
	if (asprintf(&authorization, "AWS %s:%s", key_id, authsha1_64) == -1)
		goto err0;

	/* Construct Host header. */
	if (asprintf(&host, "%s.s3.amazonaws.com", request->bucket) == -1)
		goto err1;

	/* Construct HTTP request structure. */
	RH.method = request->method;
	RH.path = request->path;
	RH.bodylen = request->bodylen;
	RH.body = request->body;

	/* We have 3 or 5 extra headers. */
	if (request->body)
		RH.nheaders = request->nheaders + 5;
	else
		RH.nheaders = request->nheaders + 3;
	if ((RH.headers =
	    malloc(RH.nheaders * sizeof(struct http_header))) == NULL)
		goto err2;

	/* Construct headers. */
	if (request->nheaders)
		memcpy(RH.headers, request->headers,
		    request->nheaders * sizeof(struct http_header));
	RH.headers[request->nheaders].header = "Host";
	RH.headers[request->nheaders].value = host;
	RH.headers[request->nheaders + 1].header = "Date";
	RH.headers[request->nheaders + 1].value = date;
	RH.headers[request->nheaders + 2].header = "Authorization";
	RH.headers[request->nheaders + 2].value = authorization;
	if (request->body) {
		RH.headers[request->nheaders + 3].header = "Content-MD5";
		RH.headers[request->nheaders + 3].value = content_md5;
		RH.headers[request->nheaders + 4].header = "Content-Length";
		RH.headers[request->nheaders + 4].value = content_length;
	}

	/* Send the request. */
	if ((http_cookie = http_request(addrs, &RH, maxrlen,
	    callback, cookie)) == NULL)
		goto err3;

	/* Free array of headers. */
	free(RH.headers);

	/* Free strings allocated by asprintf. */
	free(host);
	free(authorization);

	/* Success! */
	return (http_cookie);

err3:
	free(RH.headers);
err2:
	free(host);
err1:
	free(authorization);
err0:
	/* Failure! */
	return (NULL);
}

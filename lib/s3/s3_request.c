#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "aws_sign.h"
#include "http.h"
#include "warnp.h"

#include "s3_request.h"

/**
 * s3_request(addrs, key_id, key_secret, region, request, maxrlen,
 *     callback, cookie):
 * Using the AWS Key ID ${key_id} and Secret Access Key ${key_secret}, send
 * the S3 request ${request} to the specified S3 region.  Behave identically
 * to http_request() otherwise.
 */
void *
s3_request(struct sock_addr * const * addrs, const char * key_id,
    const char * key_secret, const char * region,
    struct s3_request * request, size_t maxrlen,
    int (* callback)(void *, struct http_response *), void * cookie)
{
	struct http_request RH;
	char * host;
	char * x_amz_content_sha256;
	char * x_amz_date;
	char * authorization;
	char content_length[sizeof(size_t) * 3 + 1];
	void * http_cookie;

	/* Construct headers needed for authorization. */
	if (aws_sign_s3_headers(key_id, key_secret, region,
	    request->method, request->bucket, request->path, request->body,
	    request->bodylen, &x_amz_content_sha256, &x_amz_date,
	    &authorization))
		goto err0;

	/* Construct Host header. */
	if (asprintf(&host, "%s.s3.amazonaws.com", request->bucket) == -1)
		goto err1;

	/* If we have a message body, construct Content-Length. */
	if (request->body)
		sprintf(content_length, "%zu", request->bodylen);

	/* Construct HTTP request structure. */
	RH.method = request->method;
	RH.path = request->path;
	RH.bodylen = request->bodylen;
	RH.body = request->body;

	/* We have 4 or 5 extra headers. */
	if (request->body)
		RH.nheaders = request->nheaders + 5;
	else
		RH.nheaders = request->nheaders + 4;
	if ((RH.headers =
	    malloc(RH.nheaders * sizeof(struct http_header))) == NULL)
		goto err2;

	/* Construct headers. */
	if (request->nheaders)
		memcpy(RH.headers, request->headers,
		    request->nheaders * sizeof(struct http_header));
	RH.headers[request->nheaders].header = "Host";
	RH.headers[request->nheaders].value = host;
	RH.headers[request->nheaders + 1].header = "X-Amz-Content-SHA256";
	RH.headers[request->nheaders + 1].value = x_amz_content_sha256;
	RH.headers[request->nheaders + 2].header = "X-Amz-Date";
	RH.headers[request->nheaders + 2].value = x_amz_date;
	RH.headers[request->nheaders + 3].header = "Authorization";
	RH.headers[request->nheaders + 3].value = authorization;
	if (request->body) {
		RH.headers[request->nheaders + 4].header = "Content-Length";
		RH.headers[request->nheaders + 4].value = content_length;
	}

	/* Send the request. */
	if ((http_cookie = http_request(addrs, &RH, maxrlen,
	    callback, cookie)) == NULL)
		goto err3;

	/* Free array of headers. */
	free(RH.headers);

	/* Free string allocated by asprintf. */
	free(host);

	/* Free headers used for authorization. */
	free(authorization);
	free(x_amz_date);
	free(x_amz_content_sha256);

	/* Success! */
	return (http_cookie);

err3:
	free(RH.headers);
err2:
	free(host);
err1:
	free(authorization);
	free(x_amz_date);
	free(x_amz_content_sha256);
err0:
	/* Failure! */
	return (NULL);
}

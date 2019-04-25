#include <stdlib.h>

#include "asprintf.h"
#include "aws_sign.h"
#include "http.h"

#include "dynamodb_request.h"

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
void *
dynamodb_request(struct sock_addr * const * addrs, const char * key_id,
    const char * key_secret, const char * region, const char * op,
    const uint8_t * body, size_t bodylen, size_t maxrlen,
    int (* callback)(void *, struct http_response *), void * cookie)
{
	struct http_request RH;
	struct http_header RHH[7];
	char * x_amz_content_sha256;
	char * x_amz_date;
	char * authorization;
	char * host;
	char * x_amz_target;
	char * content_length;
	void * http_cookie;

	/* Construct headers needed for authorization. */
	if (aws_sign_dynamodb_headers(key_id, key_secret, region, op, body,
	    bodylen, &x_amz_content_sha256, &x_amz_date, &authorization))
		goto err0;

	/* Construct Host header. */
	if (asprintf(&host, "dynamodb.%s.amazonaws.com", region) == -1)
		goto err1;

	/* Construct X-Amz-Target header. */
	if (asprintf(&x_amz_target, "DynamoDB_20120810.%s", op) == -1)
		goto err2;

	/* Construct Content-Length header. */
	if (asprintf(&content_length, "%zu", bodylen) ==  -1)
		goto err3;

	/* Construct HTTP request structure. */
	RH.method = "POST";
	RH.path = "/";
	RH.bodylen = bodylen;
	RH.body = body;
	RH.nheaders = 7;
	RH.headers = RHH;

	/* Fill in headers. */
	RHH[0].header = "Host";
	RHH[0].value = host;
	RHH[1].header = "X-Amz-Date";
	RHH[1].value = x_amz_date;
	RHH[2].header = "X-Amz-Content-SHA256";
	RHH[2].value = x_amz_content_sha256;
	RHH[3].header = "X-Amz-Target";
	RHH[3].value = x_amz_target;
	RHH[4].header = "Authorization";
	RHH[4].value = authorization;
	RHH[5].header = "Content-Length";
	RHH[5].value = content_length;
	RHH[6].header = "Content-Type";
	RHH[6].value = "application/x-amz-json-1.0";

	/* Send the request. */
	if ((http_cookie = http_request(addrs, &RH, maxrlen,
	    callback, cookie)) == NULL)
		goto err4;

	/* Free strings allocated by asprintf. */
	free(content_length);
	free(x_amz_target);
	free(host);

	/* Free headers used for authorization. */
	free(authorization);
	free(x_amz_date);
	free(x_amz_content_sha256);

	/* Success! */
	return (http_cookie);

err4:
	free(content_length);
err3:
	free(x_amz_target);
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

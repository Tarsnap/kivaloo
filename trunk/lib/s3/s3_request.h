#ifndef _S3_REQUEST_H_
#define _S3_REQUEST_H_

/* Opaque types. */
struct http_header;
struct http_response;
struct sock_addr;

struct s3_request {
	const char * method;
	const char * bucket;
	const char * path;	/* '/' for bucket, '/foo' for object. */
	size_t nheaders;
	struct http_header * headers;
	size_t bodylen;
	const uint8_t * body;
};

/**
 * s3_request(addrs, key_id, key_secret, request, maxrlen, callback, cookie):
 * Using the AWS Key ID ${key_id} and Secret Access Key ${key_secret}, send
 * the S3 request ${request}.  Behave identically to http_request otherwise.
 */
void * s3_request(struct sock_addr * const *, const char *, const char *,
    struct s3_request *, size_t,
    int (*)(void *, struct http_response *), void *);

#endif /* !_S3_REQUEST_H_ */

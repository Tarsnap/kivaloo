#ifndef _S3_VERIFYETAG_H_
#define _S3_VERIFYETAG_H_

/* Opaque types. */
struct http_response;

/**
 * s3_verifyetag(res):
 * Check if the HTTP response ${res} contains an ETag header which matches its
 * data.  Return 1 if yes, or 0 if not (i.e., if either there is no ETag, or
 * it does not match the data).
 */
int s3_verifyetag(struct http_response *);

#endif /* !_S3_VERIFYETAG_H_ */

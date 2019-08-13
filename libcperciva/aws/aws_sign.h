#ifndef _AWS_SIGN_H_
#define _AWS_SIGN_H_

#include <stddef.h>
#include <stdint.h>

/**
 * aws_sign_s3_headers(key_id, key_secret, region, method, bucket, path,
 *     body, bodylen, x_amz_content_sha256, x_amz_date, authorization):
 * Return values ${x_amz_content_sha256}, ${x_amz_date}, and ${authorization}
 * such that
 *   ${method} ${path} HTTP/1.1
 *   Host: ${bucket}.s3.amazonaws.com
 *   X-Amz-Date: ${x_amz_date}
 *   X-Amz-Content-SHA256: ${x_amz_content_sha256}
 *   Authorization: ${authorization}
 * with the addition (if ${body} != NULL) of
 *   Content-Length: ${bodylen}
 *   <${body}>
 * is a correctly signed request to the ${region} S3 region.
 */
int aws_sign_s3_headers(const char *, const char *, const char *,
    const char *, const char *, const char *, const uint8_t *, size_t,
    char **, char **, char **);

/**
 * aws_sign_s3_querystr(key_id, key_secret, region, method, bucket, path,
 *     expiry):
 * Return a query string ${query} such that
 *   ${method} http://${bucket}.s3.amazonaws.com${path}?${query}
 * is a correctly signed request which expires in ${expiry} seconds, assuming
 * that the ${bucket} S3 bucket is in region ${region}.
 */
char * aws_sign_s3_querystr(const char *, const char *, const char *,
    const char *, const char *, const char *, int);

/**
 * aws_sign_svc_headers(key_id, key_secret, region, svc, body, bodylen,
 *     x_amz_content_sha256, x_amz_date, authorization):
 * Return values ${x_amz_content_sha256}, ${x_amz_date}, and ${authorization}
 * such that
 *     POST / HTTP/1.1
 *     Host: ${svc}.${region}.amazonaws.com
 *     X-Amz-Date: ${x_amz_date}
 *     X-Amz-Content-SHA256: ${x_amz_content_sha256}
 *     Authorization: ${authorization}
 *     Content-Length: ${bodylen}
 *     <${body}>
 * is a correctly signed request to the ${region} region of the ${svc}
 * service.  This is known to be useful for API calls to EC2 and SNS.
 */
int aws_sign_svc_headers(const char *, const char *, const char *,
    const char *, const uint8_t *, size_t, char **, char **, char **);

#define aws_sign_ec2_headers(a, b, c, d, e, f, g, h) \
    aws_sign_svc_headers(a, b, c, "ec2", d, e, f, g, h)
#define aws_sign_sns_headers(a, b, c, d, e, f, g, h) \
    aws_sign_svc_headers(a, b, c, "sns", d, e, f, g, h)

/**
 * aws_sign_dynamodb_headers(key_id, key_secret, region, op, body, bodylen,
 *     x_amz_content_sha256, x_amz_date, authorization):
 * Return values ${x_amz_content_sha256}, ${x_amz_date}, and ${authorization}
 * such that
 *     POST / HTTP/1.1
 *     Host: dynamodb.${region}.amazonaws.com
 *     X-Amz-Date: ${x_amz_date}
 *     X-Amz-Content-SHA256: ${x_amz_content_sha256}
 *     X-Amz-Target: DynamoDB_20120810.${op}
 *     Authorization: ${authorization}
 *     Content-Length: ${bodylen}
 *     Content-Type: application/x-amz-json-1.0
 *     <${body}>
 * is a correctly signed request to the ${region} region of DynamoDB.
 */
int aws_sign_dynamodb_headers(const char *, const char *, const char *,
    const char *, const uint8_t *, size_t, char **, char **, char **);

#endif /* !_AWS_SIGN_H_ */

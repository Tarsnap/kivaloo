#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "asprintf.h"
#include "hexify.h"
#include "sha256.h"
#include "warnp.h"

#include "aws_sign.h"

static int
aws_sign(const char * key_secret, const char * date, const char * datetime,
    const char * region, const char * service, const char * creq,
    char sigbuf[65])
{
	char * AWS4_key;
	uint8_t kDate[32];
	uint8_t kRegion[32];
	uint8_t kService[32];
	uint8_t kSigning[32];
	uint8_t h_creq[32];
	char hhex_creq[65];
	char * STS;
	uint8_t hmac[32];

	/* Construct "AWS4" + key_secret. */
	if (asprintf(&AWS4_key, "AWS4%s", key_secret) == -1)
		goto err0;

	/* kDate = HMAC("AWS4" + kSecret, Date). */
	HMAC_SHA256_Buf(AWS4_key, strlen(AWS4_key), date, strlen(date), kDate);

	/* kRegion = HMAC(kDate, Region). */
	HMAC_SHA256_Buf(kDate, 32, region, strlen(region), kRegion);

	/* kService = HMAC(kRegion, Service). */
	HMAC_SHA256_Buf(kRegion, 32, service, strlen(service), kService);

	/* kSigning = HMAC(kService, "aws4_request"). */
	HMAC_SHA256_Buf(kService, 32, "aws4_request", strlen("aws4_request"),
	    kSigning);

	/* Free string allocated by asprintf. */
	free(AWS4_key);

	/* Generate the hexified hash of the Canonical Request string. */
	SHA256_Buf(creq, strlen(creq), h_creq);
	hexify(h_creq, hhex_creq, 32);

	/* Construct the String to Sign. */
	if (asprintf(&STS,
	    "AWS4-HMAC-SHA256\n"
	    "%s\n"
	    "%s/%s/%s/aws4_request\n"
	    "%s",
	    datetime, date, region, service, hhex_creq) == -1)
		goto err0;

	/* Sign and hexify the String to Sign. */
	HMAC_SHA256_Buf(kSigning, 32, STS, strlen(STS), hmac);
	hexify(hmac, sigbuf, 32);

	/* Free string allocated by asprintf. */
	free(STS);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

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
int
aws_sign_s3_headers(const char * key_id, const char * key_secret,
    const char * region, const char * method, const char * bucket,
    const char * path, const uint8_t * body, size_t bodylen,
    char ** x_amz_content_sha256, char ** x_amz_date, char ** authorization)
{
	time_t t_now;
	struct tm r_result;
	char date[9];
	char datetime[17];
	uint8_t hbuf[32];
	char content_sha256[65];
	char * canonical_request;
	char sigbuf[65];

	/* Get the current time. */
	if (time(&t_now) == (time_t)(-1)) {
		warnp("time");
		goto err0;
	}

	/* Construct date string <yyyymmdd>. */
	if (strftime(date, 9, "%Y%m%d", gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Construct date-and-time string <yyyymmddThhmmssZ>. */
	if (strftime(datetime, 17, "%Y%m%dT%H%M%SZ",
	    gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Compute the hexified SHA256 of the payload. */
	SHA256_Buf(body, body ? bodylen : 0, hbuf);
	hexify(hbuf, content_sha256, 32);

	/* Construct Canonical Request. */
	if (asprintf(&canonical_request,
	    "%s\n"
	    "%s\n"
	    "\n"
	    "host:%s.s3.amazonaws.com\n"
	    "x-amz-content-sha256:%s\n"
	    "x-amz-date:%s\n"
	    "\n"
	    "host;x-amz-content-sha256;x-amz-date\n"
	    "%s",
	    method, path, bucket, content_sha256, datetime,
	    content_sha256) == -1)
		goto err0;

	/* Compute request signature. */
	if (aws_sign(key_secret, date, datetime, region,
	    "s3", canonical_request, sigbuf))
		goto err1;

	/* Construct Authorization header. */
	if (asprintf(authorization,
	    "AWS4-HMAC-SHA256 "
	    "Credential=%s/%s/%s/s3/aws4_request,"
	    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
	    "Signature=%s",
	    key_id, date, region, sigbuf) == -1)
		goto err1;

	/* Duplicate X-Amz-Content-SHA256 and X-Amz-Date headers. */
	if ((*x_amz_content_sha256 = strdup(content_sha256)) == NULL)
		goto err2;
	if ((*x_amz_date = strdup(datetime)) == NULL)
		goto err3;

	/* Free string allocated by asprintf. */
	free(canonical_request);

	/* Success! */
	return (0);

err3:
	free(*x_amz_content_sha256);
err2:
	free(*authorization);
err1:
	free(canonical_request);
err0:
	/* Failure! */
	return (-1);
}

/**
 * aws_sign_s3_querystr(key_id, key_secret, region, method, bucket, path,
 *     expiry):
 * Return a query string ${query} such that
 *   ${method} http://${bucket}.s3.amazonaws.com${path}?${query}
 * is a correctly signed request which expires in ${expiry} seconds, assuming
 * that the ${bucket} S3 bucket is in region ${region}.
 */
char *
aws_sign_s3_querystr(const char * key_id, const char * key_secret,
    const char * region, const char * method, const char * bucket,
    const char * path, int expiry)
{
	time_t t_now;
	struct tm r_result;
	char date[9];
	char datetime[17];
	char * s;
	char hhex[65];

	/* Get the current time. */
	if (time(&t_now) == (time_t)(-1)) {
		warnp("time");
		goto err0;
	}

	/* Construct date string <yyyymmdd>. */
	if (strftime(date, 9, "%Y%m%d", gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Construct date-and-time string <yyyymmddThhmmssZ>. */
	if (strftime(datetime, 17, "%Y%m%dT%H%M%SZ",
	    gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Construct Canonical Request string. */
	if (asprintf(&s,
	    "%s\n"
	    "%s\n"
	    "X-Amz-Algorithm=AWS4-HMAC-SHA256&"
	    "X-Amz-Credential=%s%%2F%s%%2F%s%%2F%s%%2Faws4_request&"
	    "X-Amz-Date=%s&"
	    "X-Amz-Expires=%d&"
	    "X-Amz-SignedHeaders=host\n"
	    "host:%s.s3.amazonaws.com\n"
	    "\n"
	    "host\n"
	    "UNSIGNED-PAYLOAD",
	    method, path, key_id, date, region, "s3", datetime, expiry,
	    bucket) == -1)
		goto err0;

	if (aws_sign(key_secret, date, datetime, region, "s3", s, hhex))
		goto err1;

	/* Free the Canonical Request string. */
	free(s);

	/* Construct the query parameters. */
	if (asprintf(&s,
	    "X-Amz-Algorithm=AWS4-HMAC-SHA256&"
	    "X-Amz-Credential=%s%%2F%s%%2F%s%%2F%s%%2Faws4_request&"
	    "X-Amz-Date=%s&"
	    "X-Amz-Expires=%d&"
	    "X-Amz-SignedHeaders=host&"
	    "X-Amz-Signature=%s",
	    key_id, date, region, "s3", datetime, expiry, hhex) == -1)
		goto err0;

	/* Success! */
	return (s);

err1:
	free(s);
err0:
	/* Failure! */
	return (NULL);
}

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
 * service.  This is known to be useful for API calls to EC2, SNS, and SES.
 */
int
aws_sign_svc_headers(const char * key_id, const char * key_secret,
    const char * region, const char * svc,
    const uint8_t * body, size_t bodylen,
    char ** x_amz_content_sha256, char ** x_amz_date, char ** authorization)
{
	time_t t_now;
	struct tm r_result;
	char date[9];
	char datetime[17];
	uint8_t hbuf[32];
	char content_sha256[65];
	char * canonical_request;
	char sigbuf[65];

	/* Get the current time. */
	if (time(&t_now) == (time_t)(-1)) {
		warnp("time");
		goto err0;
	}

	/* Construct date string <yyyymmdd>. */
	if (strftime(date, 9, "%Y%m%d", gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Construct date-and-time string <yyyymmddThhmmssZ>. */
	if (strftime(datetime, 17, "%Y%m%dT%H%M%SZ",
	    gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Compute the hexified SHA256 of the payload. */
	SHA256_Buf(body, body ? bodylen : 0, hbuf);
	hexify(hbuf, content_sha256, 32);

	/* Construct Canonical Request. */
	if (asprintf(&canonical_request,
	    "POST\n"
	    "/\n"
	    "\n"
	    "host:%s.%s.amazonaws.com\n"
	    "x-amz-content-sha256:%s\n"
	    "x-amz-date:%s\n"
	    "\n"
	    "host;x-amz-content-sha256;x-amz-date\n"
	    "%s",
	    svc, region, content_sha256, datetime, content_sha256) == -1)
		goto err0;

	/* Compute request signature. */
	if (aws_sign(key_secret, date, datetime, region,
	    svc, canonical_request, sigbuf))
		goto err1;

	/* Construct Authorization header. */
	if (asprintf(authorization,
	    "AWS4-HMAC-SHA256 "
	    "Credential=%s/%s/%s/%s/aws4_request,"
	    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
	    "Signature=%s",
	    key_id, date, region, svc, sigbuf) == -1)
		goto err1;

	/* Duplicate X-Amz-Content-SHA256 and X-Amz-Date headers. */
	if ((*x_amz_content_sha256 = strdup(content_sha256)) == NULL)
		goto err2;
	if ((*x_amz_date = strdup(datetime)) == NULL)
		goto err3;

	/* Free string allocated by asprintf. */
	free(canonical_request);

	/* Success! */
	return (0);

err3:
	free(*x_amz_content_sha256);
err2:
	free(*authorization);
err1:
	free(canonical_request);
err0:
	/* Failure! */
	return (-1);
}

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
int
aws_sign_dynamodb_headers(const char * key_id, const char * key_secret,
    const char * region, const char * op,
    const uint8_t * body, size_t bodylen,
    char ** x_amz_content_sha256, char ** x_amz_date, char ** authorization)
{
	time_t t_now;
	struct tm r_result;
	char date[9];
	char datetime[17];
	uint8_t hbuf[32];
	char content_sha256[65];
	char * canonical_request;
	char sigbuf[65];

	/* Get the current time. */
	if (time(&t_now) == (time_t)(-1)) {
		warnp("time");
		goto err0;
	}

	/* Construct date string <yyyymmdd>. */
	if (strftime(date, 9, "%Y%m%d", gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Construct date-and-time string <yyyymmddThhmmssZ>. */
	if (strftime(datetime, 17, "%Y%m%dT%H%M%SZ",
	    gmtime_r(&t_now, &r_result)) == 0) {
		warnp("strftime");
		goto err0;
	}

	/* Compute the hexified SHA256 of the payload. */
	SHA256_Buf(body, body ? bodylen : 0, hbuf);
	hexify(hbuf, content_sha256, 32);

	/* Construct Canonical Request. */
	if (asprintf(&canonical_request,
	    "POST\n"
	    "/\n"
	    "\n"
	    "host:dynamodb.%s.amazonaws.com\n"
	    "x-amz-content-sha256:%s\n"
	    "x-amz-date:%s\n"
	    "x-amz-target:DynamoDB_20120810.%s\n"
	    "\n"
	    "host;x-amz-content-sha256;x-amz-date;x-amz-target\n"
	    "%s",
	    region, content_sha256, datetime, op, content_sha256) == -1)
		goto err0;

	/* Compute request signature. */
	if (aws_sign(key_secret, date, datetime, region,
	    "dynamodb", canonical_request, sigbuf))
		goto err1;

	/* Construct Authorization header. */
	if (asprintf(authorization,
	    "AWS4-HMAC-SHA256 "
	    "Credential=%s/%s/%s/dynamodb/aws4_request,"
	    "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-target,"
	    "Signature=%s",
	    key_id, date, region, sigbuf) == -1)
		goto err1;

	/* Duplicate X-Amz-Content-SHA256 and X-Amz-Date headers. */
	if ((*x_amz_content_sha256 = strdup(content_sha256)) == NULL)
		goto err2;
	if ((*x_amz_date = strdup(datetime)) == NULL)
		goto err3;

	/* Free string allocated by asprintf. */
	free(canonical_request);

	/* Success! */
	return (0);

err3:
	free(*x_amz_content_sha256);
err2:
	free(*authorization);
err1:
	free(canonical_request);
err0:
	/* Failure! */
	return (-1);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "aws_readkeys.h"
#include "aws_sign.h"
#include "insecure_memzero.h"
#include "warnp.h"

int
main(int argc, char * argv[])
{
	char * key_id;
	char * key_secret;
	char * s;
	char * x_amz_content_sha256;
	char * x_amz_date;
	char * authorization;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 4) {
		fprintf(stderr, "usage: dynamodb_sign %s %s %s\n",
		    "<keyfile>", "<region>", "<table>");
		exit(1);
	}

	/* Read AWS keys. */
	if (aws_readkeys(argv[1], &key_id, &key_secret)) {
		warnp("Failure reading AWS keys");
		exit(1);
	}

	/* Construct DynamoDB request. */
	if (asprintf(&s, "{ \"TableName\": \"%s\" }", argv[3]) == -1) {
		warnp("asprintf");
		exit(1);
	}

	/* Sign request. */
	if (aws_sign_dynamodb_headers(key_id, key_secret, argv[2],
	    "DescribeTable", (uint8_t *)s, strlen(s), &x_amz_content_sha256,
	    &x_amz_date, &authorization)) {
		warnp("Failure signing DynamoDB request");
		exit(1);
	}

	/* Output request. */
	printf("POST / HTTP/1.1\r\n"
	    "Host: dynamodb.%s.amazonaws.com\r\n"
	    "X-Amz-Date: %s\r\n"
	    "X-Amz-Content-SHA256: %s\r\n"
	    "X-Amz-Target: DynamoDB_20120810.%s\r\n"
	    "Authorization: %s\r\n"
	    "Content-Length: %zu\r\n"
	    "Content-Type: application/x-amz-json-1.0\r\n"
	    "Connection: close\r\n"
	    "\r\n"
	    "%s",
	    argv[2], x_amz_date, x_amz_content_sha256, "DescribeTable",
	    authorization, strlen(s), s);

	/* Clean up keys. */
	free(key_id);
	insecure_memzero(key_secret, strlen(key_secret));
	free(key_secret);
}

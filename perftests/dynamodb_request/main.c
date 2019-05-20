#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aws_readkeys.h"
#include "dynamodb_request.h"
#include "events.h"
#include "http.h"
#include "sock.h"
#include "warnp.h"

static int
donereq(void * cookie, struct http_response * R)
{
	int * done = cookie;
	size_t i;

	/* This request is over. */
	*done = 1;

	/* Did we succeed? */
	if (R == NULL) {
		warn0("HTTP request failed");
		return (-1);
	}

	/* Print status and headers. */
	printf("HTTP status = %d\n", R->status);
	for (i = 0; i < R->nheaders; i++)
		printf("%s\n\t%s\n",
		    R->headers[i].header, R->headers[i].value);

	/* Print the body received. */
	fwrite(R->body, 1, R->bodylen, stdout);
	fprintf(stdout, "\n");

	/* Free the body. */
	free(R->body);

	/* Success! */
	return (0);
}

int
main(int argc, char * argv[])
{
	char * key_id;
	char * key_secret;
	struct sock_addr ** sas;
	const char * body;
	int done;

	WARNP_INIT;

	/* Sanity-check. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_dynamodb %s\n", "<keyfile>");
		exit(1);
	}

	/* Read AWS keys. */
	if (aws_readkeys(argv[1], &key_id, &key_secret)) {
		warnp("Failure reading AWS keys");
		exit(1);
	}

	/* Resolve target addresses. */
	if ((sas = sock_resolve("dynamodb.us-east-1.amazonaws.com:80")) == NULL) {
		warnp("Cannot resolve DynamoDB DNS");
		exit(1);
	}

	/* Send DescribeTable request. */
	body = "{ \"TableName\": \"kivaloo-testing\" }";
	done = 0;
	(void)dynamodb_request(sas, key_id, key_secret, "us-east-1",
	    "DescribeTable", (const uint8_t *)body, strlen(body), 1024,
	    donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Send PutItem request. */
	body = "{"
		"\"TableName\": \"kivaloo-testing\","
		"\"ReturnConsumedCapacity\": \"TOTAL\","
		"\"Item\": {"
		    "\"K\": { \"S\": \"key\" },"
		    "\"V\": { \"B\": \"dmFsdWUK\" }"
		"}"
	    "}";
	done = 0;
	(void)dynamodb_request(sas, key_id, key_secret, "us-east-1",
	    "PutItem", (const uint8_t *)body, strlen(body), 1024,
	    donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Send GetItem request. */
	body = "{"
		"\"TableName\": \"kivaloo-testing\","
		"\"ReturnConsumedCapacity\": \"TOTAL\","
		"\"Key\": {"
		    "\"K\": { \"S\": \"key\" }"
		"}"
	    "}";
	done = 0;
	(void)dynamodb_request(sas, key_id, key_secret, "us-east-1",
	    "GetItem", (const uint8_t *)body, strlen(body), 1024,
	    donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free address structures. */
	sock_addr_freelist(sas);

	/* Shut down events loop (in case we're checking for memory leaks). */
	events_shutdown();

	/* Success! */
	exit(0);
}

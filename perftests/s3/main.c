#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aws_readkeys.h"
#include "events.h"
#include "http.h"
#include "s3_request.h"
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
	struct s3_request R;
	struct sock_addr ** sas;
	int done;

	WARNP_INIT;

	/* Sanity-check. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_s3 %s\n", "<keyfile>");
		exit(1);
	}

	/* Read AWS keys. */
	if (aws_readkeys(argv[1], &key_id, &key_secret)) {
		warnp("Failure reading AWS keys");
		exit(1);
	}

	/* Resolve target addresses. */
	if ((sas = sock_resolve("s3.us-west-2.amazonaws.com:80")) == NULL) {
		warnp("Cannot resolve S3 DNS");
		exit(1);
	}

	/* Construct PUT request. */
	R.method = "PUT";
	R.bucket = "kivaloo-test";
	R.path = "/nelson";
	R.nheaders = 0;
	R.headers = NULL;
	R.bodylen = strlen("ha-ha\n");
	R.body = (const uint8_t *)"ha-ha\n";

	/* Send PUT request. */
	done = 0;
	(void)s3_request(sas, key_id, key_secret, "us-west-2",
	    &R, 0, donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Convert the PUT into a GET. */
	R.method = "GET";
	R.bucket = "kivaloo-test";
	R.path = "/nelson";
	R.nheaders = 0;
	R.headers = NULL;
	R.bodylen = 0;
	R.body = NULL;

	/* Send PUT request. */
	done = 0;
	(void)s3_request(sas, key_id, key_secret, "us-west-2",
	    &R, 6, donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free address structures. */
	sock_addr_freelist(sas);

	/* Success! */
	return (0);
}

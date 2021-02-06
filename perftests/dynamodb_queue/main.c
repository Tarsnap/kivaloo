#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aws_readkeys.h"
#include "dynamodb_kv.h"
#include "dynamodb_request_queue.h"
#include "events.h"
#include "http.h"
#include "logging.h"
#include "serverpool.h"
#include "warnp.h"

static int done = 0;
static int inprogress = 0;

static int
donereq(void * cookie, struct http_response * R, const char * err)
{

	(void)cookie; /* UNUSED */
	(void)err; /* UNUSED */

	/* This request is over. */
	if (--inprogress == 0)
		done = 1;

	/* Print status. */
	printf("HTTP status = %d; ", R->status);

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
	struct serverpool * SP;
	struct logging_file * F;
	struct dynamodb_request_queue * Q;
	char * bodies[500];
	char keyname[50];
	size_t i;

	WARNP_INIT;

	/* Sanity-check. */
	if (argc != 3) {
		fprintf(stderr, "usage: test_dynamodb_queue %s %s\n",
		    "<keyfile>", "<logfile>");
		exit(1);
	}

	/* Resolve target addresses. */
	if ((SP = serverpool_create("dynamodb.us-east-1.amazonaws.com:443",
	    30, 120)) == NULL) {
		warnp("Error launching DNS lookups");
		exit(1);
	}

	/* Read AWS keys. */
	if (aws_readkeys(argv[1], &key_id, &key_secret)) {
		warnp("Failure reading AWS keys");
		exit(1);
	}

	/* Create a request queue. */
	if ((Q = dynamodb_request_queue_init(key_id, key_secret, "us-east-1",
	    SP)) == NULL) {
		warnp("Error initializing DynamoDB request queue");
		exit(1);
	}
	dynamodb_request_queue_setcapacity(Q, 5);

	/* Log requests. */
	if ((F = logging_open(argv[2])) == NULL) {
		warnp("Error initializing logging");
		exit(1);
	}
	dynamodb_request_queue_log(Q, F);

	/* Send PutItem requests. */
	done = 0;
	for (i = 0; i < 500; i++) {
		sprintf(keyname, "key%zu", i);
		if ((bodies[i] = dynamodb_kv_put("kivaloo-testing", keyname,
		    (const uint8_t *)"value\n", 6)) == NULL) {
			warnp("dynamodb_kv_put");
			exit(1);
		}
		inprogress++;
		if (dynamodb_request_queue(Q, 1, "PutItem", bodies[i], 1024,
		    keyname, donereq, &done)) {
			warnp("Error queuing DynamoDB request");
			exit(1);
		}
	}

	/* Wait for requests to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free requests. */
	for (i = 0; i < 500; i++)
		free(bodies[i]);

	/* Send GetItem requests. */
	done = 0;
	for (i = 0; i < 500; i++) {
		sprintf(keyname, "key%zu", i);
		if ((bodies[i] =
		    dynamodb_kv_get("kivaloo-testing", keyname)) == NULL) {
			warnp("dynamodb_kv_get");
			exit(1);
		}
		inprogress++;
		if (dynamodb_request_queue(Q, 1, "GetItem", bodies[i], 1024,
		    keyname, donereq, &done)) {
			warnp("Error queuing DynamoDB request");
			exit(1);
		}
	}

	/* Wait for requests to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free requests. */
	for (i = 0; i < 500; i++)
		free(bodies[i]);

	/* Send DeleteItem requests. */
	done = 0;
	for (i = 0; i < 500; i++) {
		sprintf(keyname, "key%zu", i);
		if ((bodies[i] =
		    dynamodb_kv_delete("kivaloo-testing", keyname)) == NULL) {
			warnp("dynamodb_kv_delete");
			exit(1);
		}
		inprogress++;
		if (dynamodb_request_queue(Q, 1, "DeleteItem", bodies[i], 1024,
		    keyname, donereq, &done)) {
			warnp("Error queuing DynamoDB request");
			exit(1);
		}
	}

	/* Wait for requests to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free requests. */
	for (i = 0; i < 500; i++)
		free(bodies[i]);

	/* Shut down request queue. */
	dynamodb_request_queue_free(Q);

	/* Close log file. */
	logging_close(F);

	/* Shut down the DNS lookups. */
	serverpool_free(SP);

	/* Shut down events loop (in case we're checking for memory leaks). */
	events_shutdown();

	/* Success! */
	exit(0);
}

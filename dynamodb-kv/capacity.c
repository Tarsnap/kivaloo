#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "dynamodb_request.h"
#include "dynamodb_request_queue.h"
#include "events.h"
#include "http.h"
#include "insecure_memzero.h"
#include "json.h"
#include "serverpool.h"
#include "sock.h"
#include "warnp.h"

#include "capacity.h"

/* Used for updating table throughput parameters. */
struct capacity_reader {
	char * key_id;
	char * key_secret;
	const char * tname;
	const char * rname;
	struct serverpool * SP;
	struct dynamodb_request_queue * QW;
	struct dynamodb_request_queue * QR;
	char * ddbreq;
	struct sock_addr * addrs[2];
	void * http_cookie;
	void * timer_cookie;
	int done;
};

static int callback_readmetadata(void *, struct http_response *);
static int callback_timer(void *);

/* Start describing the table. */
static int
readmetadata(struct capacity_reader * M)
{

	/* Construct the DynamoDB request body. */
	if (asprintf(&M->ddbreq, "{\"TableName\":\"%s\"}", M->tname) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Get an address. */
	if ((M->addrs[0] = serverpool_pick(M->SP)) == NULL)
		goto err1;

	/* Send the request. */
	if ((M->http_cookie = dynamodb_request(M->addrs, M->key_id,
	    M->key_secret, M->rname, "DescribeTable",
	    (const uint8_t *)M->ddbreq, strlen(M->ddbreq), 4096,
	    callback_readmetadata, M)) == NULL)
		goto err2;

	/* Success! */
	return (0);

err2:
	sock_addr_free(M->addrs[0]);
	M->addrs[0] = NULL;
err1:
	free(M->ddbreq);
	M->ddbreq = NULL;
err0:
	/* Failure! */
	return (-1);
}

/* Callback from DescribeTable. */
static int
callback_readmetadata(void * cookie, struct http_response * res)
{
	struct capacity_reader * M = cookie;
	const uint8_t * buf;
	const uint8_t * end;
	const uint8_t * capstr;
	long capr, capw;

	/* This request is no longer in progress. */
	M->http_cookie = NULL;

	/* Don't need these any more. */
	sock_addr_free(M->addrs[0]);
	M->addrs[0] = NULL;
	free(M->ddbreq);
	M->ddbreq = NULL;

	/* If we have a response, pull data out of it. */
	if ((res != NULL) && (res->body != NULL)) {
		/*
		 * Overwrite the last byte of the JSON response with a NUL;
		 * this allows us to use string functions, and it's safe
		 * since we own the buffer now anyway.
		 */
		if (res->bodylen == 0)
			goto doneparse;
		res->body[res->bodylen - 1] = '\0';

		/* Look for Table->BillingModeSummary->BillingMode. */
		buf = res->body;
		end = &buf[res->bodylen];
		buf = json_find(buf, end, "Table");
		buf = json_find(buf, end, "BillingModeSummary");
		buf = json_find(buf, end, "BillingMode");

		/*
		 * The table has unlimited capacity if we found the string
		 * "PAY_PER_REQUEST"; if we didn't find anything (possible if
		 * the table has always been in provisioned mode, since for
		 * backwards compatibility DynamoDB doesn't always fill in
		 * this field) or we found "PROVISIONED" then we have limited
		 * capacity.
		 */
		if ((&buf[17] <= end) &&
		    (memcmp(buf, "\"PAY_PER_REQUEST\"", 17) == 0)) {
			dynamodb_request_queue_setcapacity(M->QR, 0);
			dynamodb_request_queue_setcapacity(M->QW, 0);
			goto doneparse;
		}

		/* Get Table->ProvisionedThroughput. */
		buf = res->body;
		end = &buf[res->bodylen];
		buf = json_find(buf, end, "Table");
		buf = json_find(buf, end, "ProvisionedThroughput");

		/* Get ReadCapacityUnits and WriteCapacityUnits. */
		capstr = json_find(buf, end, "ReadCapacityUnits");
		capr = strtol((const char *)capstr, NULL, 10);
		capstr = json_find(buf, end, "WriteCapacityUnits");
		capw = strtol((const char *)capstr, NULL, 10);

		/* Set new capacities. */
		dynamodb_request_queue_setcapacity(M->QR, capr);
		dynamodb_request_queue_setcapacity(M->QW, capw);

doneparse:
		/* Free the response body. */
		free(res->body);

		/* We have fetched capacity parameters. */
		M->done = 1;
	}

	/*
	 * Perform another read 15 seconds from now -- unless we haven't
	 * succeeded yet, in which case wait only 1 second.
	 */
	if ((M->timer_cookie = events_timer_register_double(callback_timer,
	    M, M->done ? 15.0 : 1.0)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback from events_timer. */
static int
callback_timer(void * cookie)
{
	struct capacity_reader * M = cookie;

	/* This callback is no longer pending. */
	M->timer_cookie = NULL;

	/* Make another request. */
	return (readmetadata(M));
}

/**
 * capacity_init(key_id, key_secret, tname, rname, SP, QW, QR):
 * Using the AWS key id ${key_id} and secret key ${key_secret}, issue
 * DescribeTable requests to the DynamoDB table ${tname} in AWS region
 * ${rname}, using endpoints returned by the server pool ${SP}.  Update the
 * capacity of the write queue ${QW} and read queue ${QR}.
 *
 * Issue one request immediately, and wait for it to complete before
 * returning; issue subsequent requests every 15 seconds.
 *
 * This function may call events_run internally.
 */
struct capacity_reader *
capacity_init(const char * key_id, const char * key_secret,
    const char * tname, const char * rname, struct serverpool * SP,
    struct dynamodb_request_queue * QW, struct dynamodb_request_queue * QR)
{
	struct capacity_reader * M;

	/* Allocate space for our state and copy in parameters. */
	if ((M = malloc(sizeof(struct capacity_reader))) == NULL)
		goto err0;
	if ((M->key_id = strdup(key_id)) == NULL)
		goto err1;
	if ((M->key_secret = strdup(key_secret)) == NULL)
		goto err2;
	M->tname = tname;
	M->rname = rname;
	M->SP = SP;
	M->QW = QW;
	M->QR = QR;

	/* Initialize internal state. */
	M->ddbreq = NULL;
	M->addrs[0] = M->addrs[1] = NULL;
	M->http_cookie = NULL;
	M->timer_cookie = NULL;
	M->done = 0;

	/* Start reading table metadata. */
	if (readmetadata(M))
		goto err3;

	/* Wait for reply. */
	if (events_spin(&M->done))
		goto err0;

	/* Success! */
	return (M);

err3:
	insecure_memzero(M->key_secret, strlen(M->key_secret));
err2:
	free(M->key_id);
err1:
	free(M);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * capacity_free(M):
 * Stop issuing DescribeTable requests.
 */
void
capacity_free(struct capacity_reader * M)
{

	/* Behave consistently with free(NULL). */
	if (M == NULL)
		return;

	/* Cancel HTTP request and pending timer, if they exist. */
	if (M->http_cookie)
		http_request_cancel(M->http_cookie);
	if (M->timer_cookie)
		events_timer_cancel(M->timer_cookie);

	/* Free the AWS keys. */
	insecure_memzero(M->key_secret, strlen(M->key_secret));
	free(M->key_secret);
	free(M->key_id);

	/* Free our cookie. */
	free(M);
}

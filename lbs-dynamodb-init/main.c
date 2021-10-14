#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asprintf.h"
#include "aws_readkeys.h"
#include "dynamodb_kv.h"
#include "dynamodb_request.h"
#include "entropy.h"
#include "events.h"
#include "getopt.h"
#include "http.h"
#include "insecure_memzero.h"
#include "json.h"
#include "parsenum.h"
#include "sock.h"
#include "sysendian.h"
#include "warnp.h"

struct request_cookie {
	int done;
	int status;
	char * body;
};

static int
callback_reqdone(void * cookie, struct http_response * res)
{
	struct request_cookie * C = cookie;
	size_t i;

	/* This request has completed. */
	C->done = 1;

	/* Do we have a response? */
	if (res == NULL) {
		warn0("DynamoDB request failed");
		C->status = 0;
		goto done;
	}

	/* Sanity-check response body. */
	for (i = 0; i < res->bodylen; i++) {
		if (res->body[i] == '\0') {
			warn0("DynamoDB response contains NUL byte!");
			C->status = 0;
			free(res->body);
			goto done;
		}
	}

	/* Record status and duplicate response body. */
	C->status = res->status;
	if ((C->body = malloc(res->bodylen + 1)) == NULL)
		goto err0;
	memcpy(C->body, res->body, res->bodylen);
	C->body[res->bodylen] = '\0';

done:
	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

static char *
request(const char * key_id, const char * key_secret, const char * region,
    struct sock_addr * const * sas_ddb, const char * reqtype, const char * req)
{
	struct request_cookie C;

	/* Send the request and wait for it to complete. */
	C.done = 0;
        if (dynamodb_request(sas_ddb, key_id, key_secret, region, reqtype,
            (const uint8_t *)req, strlen(req), 4096,
            callback_reqdone, &C) == NULL) {
		warnp("Failure sending DynamoDB request");
		goto err0;
	}
	if (events_spin(&C.done)) {
		warnp("Failure running event loop");
		goto err0;
	}

	/* Did the request fail? */
	if (C.status == 0)
		goto err0;

	/* Did we get a non-200 HTTP response? */
	if (C.status != 200) {
		warn0("DynamoDB returned failure response:");
		fprintf(stderr, "%s\n", C.body);
		free(C.body);
		goto err0;
	}

	/* Success! */
	return (C.body);

err0:
	/* Failure! */
	return (NULL);
}

static int
createtable(const char * key_id, const char * key_secret, const char * region,
    struct sock_addr * const * sas_ddb, const char * tablename)
{
	char * ddbreq;
	char * body;
	const uint8_t * desc;
	const char * tablestatus;

	/* Tell the user what we're doing. */
	fprintf(stderr, "Creating table %s ", tablename);

	/* Construct a CreateTable DynamoDB request. */
	if (asprintf(&ddbreq,
	    "{"
	    "\"TableName\":\"%s\","
	    "\"AttributeDefinitions\":[{"
	        "\"AttributeName\":\"K\","
	        "\"AttributeType\":\"S\"}],"
	    "\"KeySchema\":[{"
	        "\"AttributeName\":\"K\","
	        "\"KeyType\":\"HASH\"}],"
	    "\"BillingMode\":\"PAY_PER_REQUEST\"}", tablename) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Send the request. */
	if ((body = request(key_id, key_secret, region, sas_ddb,
	    "CreateTable", ddbreq)) == NULL) {
		warnp("CreateTable failed: %s", ddbreq);
		goto err1;
	}

	/* Free request and response. */
	free(body);
	free(ddbreq);

	/* Wait until the table creation completes. */
	do {
		/* Construct a DescribeTable request. */
		if (asprintf(&ddbreq, "{\"TableName\":\"%s\"}",
		    tablename) == -1) {
			warnp("asprintf");
			goto err0;
		}

		/* Send the request. */
		if ((body = request(key_id, key_secret, region, sas_ddb,
		    "DescribeTable", ddbreq)) == NULL) {
			warnp("DescribeTable failed: %s", ddbreq);
			goto err1;
		}

		/* Find a the Table->TableStatus field. */
		desc = json_find((uint8_t *)body,
		    (uint8_t *)body + strlen(body), "Table");
		tablestatus = (const char *)json_find(desc,
		    desc + strlen((const char *)desc), "TableStatus");

		/* It should be "CREATING" or "ACTIVE". */
		if (strncmp(tablestatus, "\"CREATING\"", 10) == 0) {
			/* Wait a second and try again. */
			fprintf(stderr, ".");
			sleep(1);
			free(body);
		} else if (strncmp(tablestatus, "\"ACTIVE\"", 8) == 0) {
			/* We're done. */
			free(body);
			break;
		} else {
			/* Invalid status. */
			warn0("Unexpected DescribeTable response: %s", body);
			free(body);
			goto err1;
		}
	} while (1);

	/* Print delayed EOL. */
	fprintf(stderr, "\n");

	/* Success! */
	return (0);

err1:
	free(ddbreq);
err0:
	/* Failure! */
	return (-1);
}

static int
storetableid(const char * key_id, const char * key_secret,
    const char * region, struct sock_addr * const * sas_ddb,
    const char * tablename, uint8_t * tableid)
{
	char * ddbreq;
	char * body;

	/* Tell the user what we're doing. */
	fprintf(stderr, "Recording table ID");

	/* Construct a request to store the table ID. */
	if ((ddbreq = dynamodb_kv_create(tablename,
	    "tableid", tableid, 32)) == NULL) {
		warnp("dynamodb_kv_create");
		goto err0;
	}

	/* Send the request. */
	if ((body = request(key_id, key_secret, region, sas_ddb,
	    "PutItem", ddbreq)) == NULL) {
		warnp("Table ID PutItem failed: %s", ddbreq);
		goto err1;
	}

	/* Free request and response. */
	free(body);
	free(ddbreq);

	/* Print delayed EOL. */
	fprintf(stderr, "\n");

	/* Success! */
	return (0);

err1:
	free(ddbreq);
err0:
	/* Failure! */
	return (-1);
}

static int
createmetadata(const char * key_id, const char * key_secret,
    const char * region, struct sock_addr * const * sas_ddb,
    const char * tablename, size_t itemsz, uint8_t * tableid)
{
	uint8_t metadata[104];
	char * ddbreq;
	char * body;

	/* Tell the user what we're doing. */
	fprintf(stderr, "Storing initial metadata");

	/* Construct the metadata. */
	be64enc(&metadata[0], 0);		/* nextblk */
	be64enc(&metadata[8], 0);		/* deletedto */
	be64enc(&metadata[16], 0);		/* generation */
	be64enc(&metadata[24], (uint64_t)(-1));	/* lastblk */
	memset(&metadata[32], 0, 32);		/* process_id */
	be64enc(&metadata[64], (uint64_t)(itemsz));	/* itemsz */
	memcpy(&metadata[72], tableid, 32);	/* table ID */

	/* Construct a request to store metadata. */
	if ((ddbreq = dynamodb_kv_create(tablename, "metadata",
	    metadata, 104)) == NULL) {
		warnp("dynamodb_kv_create");
		goto err0;
	}

	/* Send the request. */
	if ((body = request(key_id, key_secret, region, sas_ddb,
	    "PutItem", ddbreq)) == NULL) {
		warnp("Metadata PutItem failed: %s", ddbreq);
		goto err1;
	}

	/* Free request and response. */
	free(body);
	free(ddbreq);

	/* Print delayed EOL. */
	fprintf(stderr, "\n");

	/* Success! */
	return (0);

err1:
	free(ddbreq);
err0:
	/* Failure! */
	return (-1);
}

static void
usage(void)
{

	fprintf(stderr, "usage: kivaloo-lbs-dynamodb-init %s %s %s %s %s\n",
	    "-k <keyfile>", "-r <region>", "-t <data table name>",
	    "-m <metadata table name>", "-b <item size>");
	fprintf(stderr, "       kivaloo-lbs-dynamodb-init --version\n");
	exit(1);
}

/* Macro to simplify error-handling in command-line parse loop. */
#define OPT_EPARSE(opt, arg) do {					\
	warnp("Error parsing argument: %s %s", opt, arg);		\
	exit(1);							\
} while (0)

int
main(int argc, char * argv[])
{

	/* Command-line parameters. */
	size_t opt_b = 0;
	char * opt_k = NULL;
	char * opt_m = NULL;
	char * opt_r = NULL;
	char * opt_t = NULL;

	/* Working variables. */
	char * dynamodb_host;
	char * key_id;
	char * key_secret;
	struct sock_addr ** sas_ddb;
	const char * ch;
	uint8_t tableid[32];

	WARNP_INIT;

	/* Parse the command line. */
	while ((ch = GETOPT(argc, argv)) != NULL) {
		GETOPT_SWITCH(ch) {
		GETOPT_OPTARG("-b"):
			if (opt_b != 0)
				usage();
			if (PARSENUM(&opt_b, optarg, 512, 8192))
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-k"):
			if (opt_k != NULL)
				usage();
			if ((opt_k = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-m"):
			if (opt_m != NULL)
				usage();
			if ((opt_m = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-r"):
			if (opt_r != NULL)
				usage();
			if ((opt_r = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPTARG("-t"):
			if (opt_t != NULL)
				usage();
			if ((opt_t = strdup(optarg)) == NULL)
				OPT_EPARSE(ch, optarg);
			break;
		GETOPT_OPT("--version"):
			fprintf(stderr, "kivaloo-lbs-dynamodb-init @VERSION@\n");
			exit(0);
		GETOPT_MISSING_ARG:
			warn0("Missing argument to %s", ch);
			usage();
		GETOPT_DEFAULT:
			warn0("illegal option -- %s", ch);
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have processed all the arguments. */
	if (argc != 0)
		usage();
	(void)argv; /* argv is not used beyond this point. */

	/* Sanity-check options. */
	if (opt_b == 0)
		usage();
	if (opt_k == NULL)
		usage();
	if (opt_m == NULL)
		usage();
	if (opt_r == NULL)
		usage();
	if (opt_t == NULL)
		usage();

	/* Warn about poor choices of block sizes. */
	if (opt_b % 1024) {
		warn0("DynamoDB item size is unlikely to be optimal: %zu",
		    opt_b);
	}

	/* Construct the DynamoDB endpoint host name. */
	if (asprintf(&dynamodb_host, "dynamodb.%s.amazonaws.com:443",
	    opt_r) == -1) {
		warnp("asprintf");
		exit(1);
	}

	/* Resolve the DynamoDB endpoint. */
	if ((sas_ddb = sock_resolve(dynamodb_host)) == NULL) {
		warnp("Error resolving DynamoDB host: %s", dynamodb_host);
		exit(1);
	}

	/* Read the key file. */
	if (aws_readkeys(opt_k, &key_id, &key_secret)) {
		warnp("Error reading AWS keys from %s", opt_k);
		exit(1);
	}

	/* Create the data and metadata tables. */
	if (createtable(key_id, key_secret, opt_r, sas_ddb, opt_t)) {
		warnp("Failed to create DynamoDB table: %s", opt_t);
		exit(1);
	}
	if (createtable(key_id, key_secret, opt_r, sas_ddb, opt_m)) {
		warnp("Failed to create DynamoDB table: %s", opt_m);
		exit(1);
	}

	/* Generate a random table ID. */
	if (entropy_read(tableid, 32)) {
		warnp("Error generating table ID");
		exit(1);
	}

	/* Record the table ID in the data table. */
	if (storetableid(key_id, key_secret, opt_r, sas_ddb, opt_t, tableid)) {
		warnp("Failed to store table ID");
		exit(1);
	}

	/* Store a metadata blob in the metadata table. */
	if (createmetadata(key_id, key_secret, opt_r, sas_ddb, opt_m,
	    opt_b, tableid)) {
		warnp("Failed to store metadata");
		exit(1);
	}

	/* Free the address structures. */
	sock_addr_freelist(sas_ddb);

	/* Free string allocated by asprintf. */
	free(dynamodb_host);

	/* Free key strings. */
	free(key_id);
	insecure_memzero(key_secret, strlen(key_secret));
	free(key_secret);

	/* Free option strings. */
	free(opt_k);
	free(opt_m);
	free(opt_r);
	free(opt_t);

	/* Success! */
	exit(0);
}

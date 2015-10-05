#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "proto_s3.h"
#include "sock.h"
#include "sysendian.h"
#include "wire.h"
#include "warnp.h"

static int opdone;

static int
callback_put(void * cookie, int failed)
{

	(void)cookie; /* UNUSED */

	/* Print status. */
	printf("PUT failed = %d\n", failed);

	/* We're done. */
	opdone = 1;
	return (0);
}

static int
callback_get(void * cookie, int failed, size_t len, const uint8_t * buf)
{

	(void)cookie; /* UNUSED */

	/* Print status. */
	printf("GET failed = %d len = %zd\n", failed, len);

	/* Print data. */
	if (buf != NULL) {
		printf("GET data = >>>");
		fwrite(buf, len, 1, stdout);
		printf("<<<\n");
	} else {
		printf("GET data = NULL\n");
	}

	/* We're done. */
	opdone = 1;
	return (0);
}

static int
callback_range(void * cookie, int failed, size_t buflen, const uint8_t * buf)
{

	(void)cookie; /* UNUSED */

	/* Print status. */
	printf("RANGE failed = %d buflen = %zd\n", failed, buflen);

	/* Print data. */
	if (buf != NULL) {
		printf("RANGE data = >>>");
		fwrite(buf, buflen, 1, stdout);
		printf("<<<\n");
	} else {
		printf("RANGE data = NULL\n");
	}

	/* We're done. */
	opdone = 1;
	return (0);
}

static int
callback_head(void * cookie, int status, size_t len)
{

	(void)cookie; /* UNUSED */

	/* Print status. */
	printf("HEAD status = %d len = %zd\n", status, len);

	/* We're done. */
	opdone = 1;
	return (0);
}

static int
callback_delete(void * cookie, int failed)
{

	(void)cookie; /* UNUSED */

	/* Print status. */
	printf("DELETE failed = %d\n", failed);

	/* We're done. */
	opdone = 1;
	return (0);
}

static void
readtests(struct wire_requestqueue * Q, const char * bucket)
{

	/* GET with a large buffer. */
	opdone = 0;
	if (proto_s3_request_get(Q, bucket, "s3-testfile", 100,
	    callback_get, NULL) ||
	    events_spin(&opdone)) {
		warn0("GET failed");
		exit(1);
	}

	/* GET with a small buffer. */
	opdone = 0;
	if (proto_s3_request_get(Q, bucket, "s3-testfile", 10,
	    callback_get, NULL) ||
	    events_spin(&opdone)) {
		warn0("GET failed");
		exit(1);
	}

	/* RANGE. */
	opdone = 0;
	if (proto_s3_request_range(Q, bucket, "s3-testfile", 6, 5,
	    callback_range, NULL) ||
	    events_spin(&opdone)) {
		warn0("RANGE failed");
		exit(1);
	}

	/* HEAD. */
	opdone = 0;
	if (proto_s3_request_head(Q, bucket, "s3-testfile",
	    callback_head, NULL) ||
	    events_spin(&opdone)) {
		warn0("HEAD failed");
		exit(1);
	}
}

static void
putfile(struct wire_requestqueue * Q, const char * bucket)
{

	/* PUT the object. */
	opdone = 0;
	if (proto_s3_request_put(Q, bucket, "s3-testfile", 11,
	    (const uint8_t *)"hello world", callback_put, NULL) ||
	    events_spin(&opdone)) {
		warn0("PUT failed");
		exit(1);
	}
}

static void
deletefile(struct wire_requestqueue * Q, const char * bucket)
{

	/* DELETE the object. */
	opdone = 0;
	if (proto_s3_request_delete(Q, bucket, "s3-testfile",
	    callback_delete, NULL) ||
	    events_spin(&opdone)) {
		warn0("DELETE failed");
		exit(1);
	}
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 3) {
		fprintf(stderr, "usage: test_s3 %s %s\n",
		    "<socketname>", "<bucket>");
		exit(1);
	}

	/* Resolve the socket address and connect. */
	if ((sas = sock_resolve(argv[1])) == NULL) {
		warnp("Error resolving socket address: %s", argv[1]);
		exit(1);
	}
	if (sas[0] == NULL) {
		warn0("No addresses found for %s", argv[1]);
		exit(1);
	}
	if ((s = sock_connect(sas)) == -1)
		exit(1);

	/* Create a request queue. */
	if ((Q = wire_requestqueue_init(s)) == NULL) {
		warnp("Cannot create packet write queue");
		exit(1);
	}

	/* Do read tests; these should all fail. */
	readtests(Q, argv[2]);

	/* PUT the file. */
	putfile(Q, argv[2]);

	/* Do read tests; they should succeed this time. */
	readtests(Q, argv[2]);

	/* DELETE the file. */
	deletefile(Q, argv[2]);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Close all streams, in order to free malloced internal buffer. */
	fcloseall();

	/* Success! */
	exit(0);
}

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "kivaloo.h"
#include "proto_lbs.h"
#include "warnp.h"

/* The tests will never reach this block number. */
#define BAD_BLKNO (UINT64_MAX - 2)

/* Total of 256 pages. */
static size_t npages[] = {
	15, 1, 2, 14, 13, 3, 4, 12, 8, 8, 8, 8, 8, 8, 8, 8,
	3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3, 2, 3, 8, 4, 6,
	10, 15, 0};

static int params_done;
static int params_failed;
static size_t params_blklen;
static uint64_t params_nextblk;
static int append_done;
static int append_failed;
static int append_bad_start_response;
static int get_done;
static int get_failed;
static int get_not_exist_response;
static int gets_done;
static int gets_failed;
static int gets_ndone;
static int free_done;
static int free_failed;

/* Callback for PARAMS request. */
static int
callback_params(void * cookie, int failed, size_t blklen, uint64_t blkno)
{

	(void)cookie; /* UNUSED */

	/* Record returned values. */
	params_failed = failed;
	params_blklen = blklen;
	params_nextblk = blkno;

	/* We're done. */
	params_done = 1;

	/* Success! */
	return (0);
}

/* Callback for APPEND request. */
static int
callback_append(void * cookie, int failed, int status, uint64_t blkno)
{

	(void)cookie; /* UNUSED */

	/* Record returned values. */
	append_failed = failed;
	if (status)
		append_failed = 1;
	params_nextblk = blkno;

	/* We're done. */
	append_done = 1;

	/* Success! */
	return (0);
}

/* Callback for APPEND request for "the starting block # is incorrect". */
static int
callback_append_should_bad_start(void * cookie, int failed, int status,
    uint64_t blkno)
{

	(void)cookie; /* UNUSED */
	(void)blkno; /* UNUSED */

	/* Parsed the response? */
	append_failed = failed;

	/* The status should be "the starting block # is incorrect". */
	if (status != 1)
		append_bad_start_response = 1;

	/* We're done. */
	append_done = 1;

	/* Success! */
	return (0);
}

/* Callback for GET request. */
static int
callback_get(void * cookie, int failed, int status, const uint8_t * buf)
{
	uint8_t * dstbuf = cookie;

	/* Record returned values. */
	get_failed = failed;
	if (status)
		get_failed = 1;
	if (failed == 0)
		memcpy(dstbuf, buf, params_blklen);

	/* We're done. */
	get_done = 1;

	/* Success! */
	return (0);
}

/* Callback for simultaneous GET requests. */
static int
callback_gets(void * cookie, int failed, int status, const uint8_t * buf)
{
	int val = (int)(uintptr_t)cookie;

	/* Did we fail? */
	if (failed)
		gets_failed = 1;
	else if (status)
		gets_failed = 1;
	else if (buf[0] != val)
		gets_failed = 1;

	/* We're done this GET. */
	gets_ndone += 1;
	if (gets_ndone == 256)
		gets_done = 1;

	/* Success! */
	return (0);
}

/* Callback for GET request for "block does not exist". */
static int
callback_get_should_not_exist(void * cookie, int failed, int status,
    const uint8_t * buf)
{

	(void)cookie; /* UNUSED */
	(void)buf; /* UNUSED */

	/* Parsed the response? */
	get_failed = failed;

	/* The status should be "block does not exist". */
	if (status != 1)
		get_not_exist_response = 1;

	/* We're done. */
	get_done = 1;

	/* Success! */
	return (0);
}

/* Callback for FREE request. */
static int
callback_free(void * cookie, int failed)
{

	(void)cookie; /* UNUSED */

	/* Record returned value. */
	free_failed = failed;

	/* We're done. */
	free_done = 1;

	/* Success! */
	return (0);
}

int
main(int argc, char * argv[])
{
	struct wire_requestqueue * Q;
	struct kivaloo_cookie * K;
	uint8_t * buf;
	size_t i, j, k;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_lbs %s\n", "<socketname>");
		goto err0;
	}

	/* Open a connection to LBS. */
	if ((K = kivaloo_open(argv[1], &Q)) == NULL) {
		warnp("Could not connect to LBS daemon.");
		goto err0;
	}

	/* Send a PARAMS request and wait for it to complete. */
	params_done = params_failed = 0;
	if (proto_lbs_request_params(Q, callback_params, NULL)) {
		warnp("Failed to send PARAMS request");
		goto err1;
	}
	if (events_spin(&params_done) || params_failed) {
		warnp("PARAMS request failed");
		goto err1;
	}

	/* Allocate 16 blocks. */
	if ((buf = malloc(16 * params_blklen)) == NULL) {
		warnp("malloc");
		goto err1;
	}

	/* Write 256 pages in batches of various sizes. */
	for (i = k = 0; npages[i] != 0; i++) {
		for (j = 0; j < npages[i]; j++)
			memset(&buf[j * params_blklen],
			    (int)(k + j), params_blklen);
		k += npages[i];
		append_done = append_failed = 0;
		if (proto_lbs_request_append(Q, (uint32_t)npages[i],
		    params_nextblk, params_blklen, buf, callback_append,
		    NULL)) {
			warnp("Failed to send APPEND request");
			goto err2;
		}
		if (events_spin(&append_done) || append_failed) {
			warnp("APPEND request failed");
			goto err2;
		}
	}

	/* Write 256 pages individually. */
	memset(buf, 0, params_blklen);
	for (i = 0; i < 256; i++) {
		append_done = append_failed = 0;
		if (proto_lbs_request_append(Q, 1, params_nextblk,
		    params_blklen, buf, callback_append, NULL)) {
			warnp("Failed to send APPEND request");
			goto err2;
		}
		if (events_spin(&append_done) || append_failed) {
			warnp("APPEND request failed");
			goto err2;
		}
	}

	/* Attempt to write with a bad starting block #. */
	append_done = append_failed = append_bad_start_response = 0;
	if (proto_lbs_request_append(Q, 1, BAD_BLKNO, params_blklen,
	    buf, callback_append_should_bad_start, NULL)) {
		warnp("Failed to send APPEND request");
		goto err2;
	}
	if (events_spin(&append_done) || append_failed) {
		warnp("APPEND request failed");
		goto err2;
	}
	if (append_bad_start_response) {
		warnp("APPEND request failed to return bad-starting-blkno");
		goto err2;
	}

	/* Read pages sequentially. */
	for (i = 0; i < 512; i++) {
		get_done = get_failed = 0;
		if (proto_lbs_request_get(Q, params_nextblk - 512 + i,
		    params_blklen, callback_get, buf)) {
			warnp("Failed to send GET request");
			goto err2;
		}
		if (events_spin(&get_done) || get_failed) {
			warnp("GET request failed");
			goto err2;
		}
		if (((i < 256) && (buf[0] != i)) ||
		    ((i >= 256) && (buf[0] != 0))) {
			warn0("GET data is incorrect");
			warn0("i = %d buf[0] = %d", (int)(i), (int)(buf[0]));
			goto err2;
		}
	}

	/* Read 256 pages at once. */
	gets_done = gets_failed = gets_ndone = 0;
	for (i = 0; i < 256; i++) {
		if (proto_lbs_request_get(Q, params_nextblk - 512 + i,
		    params_blklen, callback_gets, (void *)(uintptr_t)(i))) {
			warnp("Failed to send GET request");
			goto err2;
		}
	}
	if (events_spin(&gets_done) || gets_failed) {
		warnp("GET request(s) failed");
		goto err2;
	}

	/* Attempt to read a non-existent block. */
	get_done = get_failed = get_not_exist_response = 0;
	if (proto_lbs_request_get(Q, BAD_BLKNO, params_blklen,
	    callback_get_should_not_exist, NULL)) {
		warnp("Failed to send GET request");
		goto err2;
	}
	if (events_spin(&get_done) || get_failed) {
		warnp("GET request failed");
		goto err2;
	}
	if (get_not_exist_response) {
		warnp("GET request failed to return does-not-exist");
		goto err2;
	}

	/* Free blocks. */
	free_done = free_failed = 0;
	if (proto_lbs_request_free(Q, params_nextblk, callback_free, NULL)) {
		warnp("Failed to send FREE request");
		goto err2;
	}
	if (events_spin(&free_done) || free_failed) {
		warnp("FREE request failed");
		goto err2;
	}

	/* Free the request queue and network connection. */
	kivaloo_close(K);

	/* Free buffer used for holding blocks. */
	free(buf);

	/* Success! */
	exit(0);

err2:
	free(buf);
err1:
	kivaloo_close(K);
err0:
	/* Failure! */
	exit(1);
}

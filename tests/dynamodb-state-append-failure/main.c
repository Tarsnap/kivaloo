/*
 * Regression for DynamoDB append failure unwinding.
 *
 * Compile the real lbs-dynamodb/state.c with deterministic request/metadata
 * seams.  Exercise a failure before any block PUT is queued, a partial PUT
 * scheduling failure, an asynchronous block failure, a lastblk scheduling
 * failure, and the full success path.
 */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define events_spin test_events_spin
#define metadata_nextblk_read test_metadata_nextblk_read
#define metadata_nextblk_write test_metadata_nextblk_write
#define metadata_lastblk_read test_metadata_lastblk_read
#define metadata_lastblk_write test_metadata_lastblk_write
#define objmap test_objmap
#define proto_dynamodb_kv_request_get test_request_get
#define proto_dynamodb_kv_request_getc test_request_getc
#define proto_dynamodb_kv_request_put test_request_put

#include "../../lbs-dynamodb/state.c"

#undef proto_dynamodb_kv_request_put
#undef proto_dynamodb_kv_request_getc
#undef proto_dynamodb_kv_request_get
#undef objmap
#undef metadata_lastblk_write
#undef metadata_lastblk_read
#undef metadata_nextblk_write
#undef metadata_nextblk_read
#undef events_spin

struct put_capture {
	int (* callback)(void *, int);
	void * cookie;
};

static int nextblk_start_fail;
static int put_fail_at;
static int lastblk_start_fail;
static size_t put_calls;
static size_t nputs;
static size_t lastblk_calls;
static uint64_t lastblk_value;
static int (* nextblk_callback)(void *);
static void * nextblk_cookie;
static int (* lastblk_callback)(void *);
static void * lastblk_cookie;
static struct put_capture put_caps[8];
static size_t user_callbacks;
static uint64_t user_nextblk;

static void
reset_capture(void)
{

	nextblk_start_fail = 0;
	put_fail_at = 0;
	lastblk_start_fail = 0;
	put_calls = 0;
	nputs = 0;
	lastblk_calls = 0;
	lastblk_value = 0;
	nextblk_callback = NULL;
	nextblk_cookie = NULL;
	lastblk_callback = NULL;
	lastblk_cookie = NULL;
	memset(put_caps, 0, sizeof(put_caps));
	user_callbacks = 0;
	user_nextblk = 0;
}

static void
setup(struct state * S, struct proto_lbs_request * R, uint8_t * data,
    uint32_t nblks)
{

	memset(S, 0, sizeof(*S));
	memset(R, 0, sizeof(*R));
	S->blklen = 4;
	S->lastblk = 9;
	S->nextblk = 10;
	S->Q = (struct wire_requestqueue *)(uintptr_t)1;
	S->M = (struct metadata *)(uintptr_t)2;
	R->r.append.nblks = nblks;
	R->r.append.buf = data;
}

static int
user_callback(void * cookie, struct proto_lbs_request * R, uint64_t nextblk)
{

	(void)cookie;
	assert(R != NULL);
	user_callbacks++;
	user_nextblk = nextblk;
	return (0);
}

int
test_events_spin(const int * done)
{

	(void)done;
	return (-1);
}

uint64_t
test_metadata_nextblk_read(struct metadata * M)
{

	(void)M;
	return (10);
}

uint64_t
test_metadata_lastblk_read(struct metadata * M)
{

	(void)M;
	return (9);
}

int
test_metadata_nextblk_write(struct metadata * M, uint64_t nextblk,
    int (* callback)(void *), void * cookie)
{

	(void)M;
	assert(nextblk >= 10);
	if (nextblk_start_fail)
		return (-1);
	nextblk_callback = callback;
	nextblk_cookie = cookie;
	return (0);
}

int
test_metadata_lastblk_write(struct metadata * M, uint64_t lastblk,
    int (* callback)(void *), void * cookie)
{

	(void)M;
	lastblk_calls++;
	lastblk_value = lastblk;
	if (lastblk_start_fail)
		return (-1);
	lastblk_callback = callback;
	lastblk_cookie = cookie;
	return (0);
}

const char *
test_objmap(uint64_t blkno)
{

	(void)blkno;
	return ("block");
}

int
test_request_get(struct wire_requestqueue * Q, const char * key,
    int (* callback)(void *, int, const uint8_t *, size_t), void * cookie)
{

	(void)Q;
	(void)key;
	(void)callback;
	(void)cookie;
	return (-1);
}

int
test_request_getc(struct wire_requestqueue * Q, const char * key,
    int (* callback)(void *, int, const uint8_t *, size_t), void * cookie)
{

	return (test_request_get(Q, key, callback, cookie));
}

int
test_request_put(struct wire_requestqueue * Q, const char * key,
    const uint8_t * buf, size_t buflen, int (* callback)(void *, int),
    void * cookie)
{

	(void)Q;
	(void)key;
	(void)buf;
	(void)buflen;
	put_calls++;
	if ((put_fail_at != 0) && (put_calls == (size_t)put_fail_at))
		return (-1);
	assert(nputs < sizeof(put_caps) / sizeof(put_caps[0]));
	put_caps[nputs].callback = callback;
	put_caps[nputs].cookie = cookie;
	nputs++;
	return (0);
}

void
libcperciva_warn(const char * format, ...)
{

	(void)format;
}

void
libcperciva_warnx(const char * format, ...)
{

	(void)format;
}

static void
test_initial_metadata_failure(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[8] = {0};

	reset_capture();
	setup(&S, &R, data, 2);
	nextblk_start_fail = 1;
	assert(state_append(&S, &R, user_callback, NULL) == -1);
	assert(S.nextblk == 10);
	assert(S.lastblk == 9);
	assert(S.npending == 0);
	assert(user_callbacks == 0);
}

static void
test_no_put_scheduled(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[8] = {0};

	reset_capture();
	setup(&S, &R, data, 2);
	put_fail_at = 1;
	assert(state_append(&S, &R, user_callback, NULL) == 0);
	assert(S.npending == 1);
	assert(nextblk_callback(nextblk_cookie) == -1);
	assert(nputs == 0);
	assert(S.npending == 0);
	assert(user_callbacks == 0);
}

static void
test_partial_schedule_failure(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[16] = {0};

	reset_capture();
	setup(&S, &R, data, 4);
	put_fail_at = 3;
	assert(state_append(&S, &R, user_callback, NULL) == 0);
	assert(nextblk_callback(nextblk_cookie) == 0);
	assert(nputs == 2);
	assert(S.npending == 1);
	assert(put_caps[0].callback(put_caps[0].cookie, 0) == 0);
	assert(S.npending == 1);
	assert(put_caps[1].callback(put_caps[1].cookie, 0) == -1);
	assert(S.npending == 0);
	assert(lastblk_calls == 0);
	assert(user_callbacks == 0);
}

static void
test_async_put_failure(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[12] = {0};

	reset_capture();
	setup(&S, &R, data, 3);
	assert(state_append(&S, &R, user_callback, NULL) == 0);
	assert(nextblk_callback(nextblk_cookie) == 0);
	assert(nputs == 3);
	assert(put_caps[0].callback(put_caps[0].cookie, 1) == 0);
	assert(put_caps[1].callback(put_caps[1].cookie, 0) == 0);
	assert(put_caps[2].callback(put_caps[2].cookie, 0) == -1);
	assert(S.npending == 0);
	assert(lastblk_calls == 0);
	assert(user_callbacks == 0);
}

static void
test_lastblk_schedule_failure(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[8] = {0};

	reset_capture();
	setup(&S, &R, data, 2);
	lastblk_start_fail = 1;
	assert(state_append(&S, &R, user_callback, NULL) == 0);
	assert(nextblk_callback(nextblk_cookie) == 0);
	assert(put_caps[0].callback(put_caps[0].cookie, 0) == 0);
	assert(put_caps[1].callback(put_caps[1].cookie, 0) == -1);
	assert(lastblk_calls == 1);
	assert(lastblk_value == 11);
	assert(S.lastblk == 9);
	assert(S.npending == 0);
	assert(user_callbacks == 0);
}

static void
test_success(void)
{
	struct state S;
	struct proto_lbs_request R;
	uint8_t data[8] = {0};

	reset_capture();
	setup(&S, &R, data, 2);
	assert(state_append(&S, &R, user_callback, NULL) == 0);
	assert(nextblk_callback(nextblk_cookie) == 0);
	assert(nputs == 2);
	assert(put_caps[0].callback(put_caps[0].cookie, 0) == 0);
	assert(put_caps[1].callback(put_caps[1].cookie, 0) == 0);
	assert(lastblk_calls == 1);
	assert(lastblk_value == 11);
	assert(S.lastblk == 11);
	assert(S.npending == 1);
	assert(lastblk_callback(lastblk_cookie) == 0);
	assert(S.npending == 0);
	assert(user_callbacks == 1);
	assert(user_nextblk == 12);
}

int
main(void)
{

	test_initial_metadata_failure();
	test_no_put_scheduled();
	test_partial_schedule_failure();
	test_async_put_failure();
	test_lastblk_schedule_failure();
	test_success();
	puts("PASS: DynamoDB append failure paths drain shared callbacks once");
	return (0);
}

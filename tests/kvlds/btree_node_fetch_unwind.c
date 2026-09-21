/*
 * Regression for btree_node_fetch_canfail() startup-failure unwinding.
 *
 * Compile the real btree_node.c and inject each failure which can occur
 * after makepresent() has locked p_shadow and p_dirty but before a page read
 * is established.  Every failure must restore both parent lock counts and
 * remove the target node from the pool.
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/datastruct/kvldskey.h"
#include "../../lib/datastruct/kvpair.h"
#include "../../lib/datastruct/pool.h"
#include "../../kvlds/btree.h"
#include "../../kvlds/node.h"

enum failpoint {
	FAIL_READING_ALLOC = 1,
	FAIL_READERLIST_INIT,
	FAIL_REQUEST_GET
};

static struct node * target_node;
static struct pool_elem target_elem;
static enum failpoint active_failpoint;
static size_t malloc_calls;
static size_t readerlist_init_calls;
static size_t readerlist_free_calls;
static size_t request_get_calls;
static size_t pool_add_calls;
static size_t pool_free_calls;
static size_t queue_calls;

static void * test_malloc(size_t);

#define malloc test_malloc
#include "../../kvlds/btree_node.c"
#undef malloc

static void *
test_malloc(size_t len)
{

	malloc_calls++;
	if (active_failpoint == FAIL_READING_ALLOC) {
		errno = ENOMEM;
		return (NULL);
	}
	return (malloc(len));
}

struct node *
node_alloc(uint64_t pagenum, uint64_t oldestleaf, uint32_t pagesize)
{

	(void)pagenum;
	(void)oldestleaf;
	(void)pagesize;
	assert(0 && "node_alloc unexpectedly called");
	return (NULL);
}

void
node_free(struct node * N)
{

	(void)N;
	assert(0 && "node_free unexpectedly called");
}

int
pool_rec_add(struct pool * P, void * rec, void ** evict)
{
	struct pool_elem ** E;

	assert(rec == target_node);
	E = (struct pool_elem **)(void *)((uint8_t *)rec + P->offset);
	assert(*E == NULL);
	memset(&target_elem, 0, sizeof(target_elem));
	target_elem.wire_count = 1;
	*E = &target_elem;
	*evict = NULL;
	pool_add_calls++;
	return (0);
}

void
pool_rec_free(struct pool * P, void * rec)
{
	struct pool_elem ** E;

	assert(rec == target_node);
	E = (struct pool_elem **)(void *)((uint8_t *)rec + P->offset);
	assert(*E == &target_elem);
	assert(target_elem.wire_count == 1);
	*E = NULL;
	pool_free_calls++;
}

size_t
pool_rec_lockcount(struct pool * P, void * rec)
{

	return (get_pool_elem(P, rec)->wire_count);
}

void
pool_addqueue(struct pool * P, void * rec)
{

	(void)P;
	(void)rec;
	queue_calls++;
}

void
pool_delqueue(struct pool * P, void * rec)
{

	(void)P;
	(void)rec;
	queue_calls++;
}

void
btree_cleaning_notify_dirtying(struct cleaner * C, struct node * N)
{

	(void)C;
	(void)N;
}

size_t
kvldskey_mlen(const struct kvldskey * K1, const struct kvldskey * K2)
{

	(void)K1;
	(void)K2;
	return (0);
}

int
proto_lbs_request_get(struct wire_requestqueue * Q, uint64_t blkno,
    size_t blklen, int (* callback)(void *, int, int, const uint8_t *),
    void * cookie)
{

	(void)Q;
	(void)blkno;
	(void)blklen;
	(void)callback;
	(void)cookie;
	request_get_calls++;
	assert(active_failpoint == FAIL_REQUEST_GET);
	return (-1);
}

void *
events_immediate_register(int (* callback)(void *), void * cookie, int prio)
{

	(void)callback;
	(void)cookie;
	(void)prio;
	assert(0 && "events_immediate_register unexpectedly called");
	return (NULL);
}

int
deserialize(struct node * N, const uint8_t * buf, size_t buflen)
{

	(void)N;
	(void)buf;
	(void)buflen;
	assert(0 && "deserialize unexpectedly called");
	return (-1);
}

int
deserialize_root(struct btree * T, const uint8_t * buf)
{

	(void)T;
	(void)buf;
	assert(0 && "deserialize_root unexpectedly called");
	return (-1);
}

struct elasticarray *
elasticarray_init(size_t nrec, size_t reclen)
{

	(void)nrec;
	(void)reclen;
	readerlist_init_calls++;
	if (active_failpoint == FAIL_READERLIST_INIT) {
		errno = ENOMEM;
		return (NULL);
	}
	assert(active_failpoint == FAIL_REQUEST_GET);
	return ((struct elasticarray *)(uintptr_t)1);
}

int
elasticarray_resize(struct elasticarray * EA, size_t nrec, size_t reclen)
{

	(void)EA;
	(void)nrec;
	(void)reclen;
	assert(0 && "elasticarray_resize unexpectedly called");
	return (-1);
}

size_t
elasticarray_getsize(const struct elasticarray * EA, size_t reclen)
{

	(void)EA;
	(void)reclen;
	assert(0 && "elasticarray_getsize unexpectedly called");
	return (0);
}

int
elasticarray_append(struct elasticarray * EA, const void * buf, size_t nrec,
    size_t reclen)
{

	(void)EA;
	(void)buf;
	(void)nrec;
	(void)reclen;
	assert(0 && "elasticarray_append unexpectedly called");
	return (-1);
}

void
elasticarray_shrink(struct elasticarray * EA, size_t nrec, size_t reclen)
{

	(void)EA;
	(void)nrec;
	(void)reclen;
	assert(0 && "elasticarray_shrink unexpectedly called");
}

int
elasticarray_truncate(struct elasticarray * EA)
{

	(void)EA;
	assert(0 && "elasticarray_truncate unexpectedly called");
	return (-1);
}

void *
elasticarray_get(struct elasticarray * EA, size_t pos, size_t reclen)
{

	(void)EA;
	(void)pos;
	(void)reclen;
	assert(0 && "elasticarray_get unexpectedly called");
	return (NULL);
}

void
elasticarray_free(struct elasticarray * EA)
{

	assert(EA == (struct elasticarray *)(uintptr_t)1);
	readerlist_free_calls++;
}

int
elasticarray_export(struct elasticarray * EA, void ** buf, size_t * nrec,
    size_t reclen)
{

	(void)EA;
	(void)buf;
	(void)nrec;
	(void)reclen;
	assert(0 && "elasticarray_export unexpectedly called");
	return (-1);
}

int
elasticarray_exportdup(const struct elasticarray * EA, void ** buf,
    size_t * nrec, size_t reclen)
{

	(void)EA;
	(void)buf;
	(void)nrec;
	(void)reclen;
	assert(0 && "elasticarray_exportdup unexpectedly called");
	return (-1);
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

static int
dummy_callback(void * cookie)
{

	(void)cookie;
	assert(0 && "fetch callback unexpectedly called");
	return (-1);
}

static int
run_case(const char * name, enum failpoint fp)
{
	struct btree T;
	struct pool P;
	struct node N;
	struct node shadow_parent;
	struct node dirty_parent;
	struct pool_elem shadow_elem;
	struct pool_elem dirty_elem;
	size_t expected_readerlist_inits;
	size_t expected_readerlist_frees;
	size_t expected_requests;
	int rc;

	memset(&T, 0, sizeof(T));
	memset(&P, 0, sizeof(P));
	memset(&N, 0, sizeof(N));
	memset(&shadow_parent, 0, sizeof(shadow_parent));
	memset(&dirty_parent, 0, sizeof(dirty_parent));
	memset(&shadow_elem, 0, sizeof(shadow_elem));
	memset(&dirty_elem, 0, sizeof(dirty_elem));

	P.offset = offsetof(struct node, pool_cookie);
	T.P = &P;
	T.pagelen = 4096;

	shadow_parent.type = NODE_TYPE_PARENT;
	shadow_parent.state = NODE_STATE_CLEAN;
	shadow_parent.pool_cookie = &shadow_elem;
	shadow_elem.wire_count = 1;

	dirty_parent.type = NODE_TYPE_PARENT;
	dirty_parent.state = NODE_STATE_DIRTY;
	dirty_parent.pool_cookie = &dirty_elem;
	dirty_elem.wire_count = 1;

	N.pagenum = 17;
	N.type = NODE_TYPE_NP;
	N.state = NODE_STATE_CLEAN;
	N.height = -1;
	N.nkeys = (size_t)(-1);
	N.p_shadow = &shadow_parent;
	N.p_dirty = &dirty_parent;
	N.pool_cookie = NULL;

	target_node = &N;
	active_failpoint = fp;
	malloc_calls = 0;
	readerlist_init_calls = 0;
	readerlist_free_calls = 0;
	request_get_calls = 0;
	pool_add_calls = 0;
	pool_free_calls = 0;
	queue_calls = 0;

	rc = btree_node_fetch_canfail(&T, &N, dummy_callback, NULL, 1);

	expected_readerlist_inits =
	    (fp == FAIL_READING_ALLOC) ? 0 : 1;
	expected_readerlist_frees =
	    (fp == FAIL_REQUEST_GET) ? 1 : 0;
	expected_requests =
	    (fp == FAIL_REQUEST_GET) ? 1 : 0;

	if (rc != -1) {
		fprintf(stderr, "%s: fetch unexpectedly returned %d\n", name, rc);
		return (-1);
	}
	if (N.type != NODE_TYPE_NP) {
		fprintf(stderr, "%s: target type changed to %u\n", name, N.type);
		return (-1);
	}
	if (N.pool_cookie != NULL) {
		fprintf(stderr, "%s: target remained in page pool\n", name);
		return (-1);
	}
	if (shadow_elem.wire_count != 1) {
		fprintf(stderr, "%s: shadow-parent lock count %zu, expected 1\n",
		    name, shadow_elem.wire_count);
		return (-1);
	}
	if (dirty_elem.wire_count != 1) {
		fprintf(stderr, "%s: dirty-parent lock count %zu, expected 1\n",
		    name, dirty_elem.wire_count);
		return (-1);
	}
	if ((pool_add_calls != 1) || (pool_free_calls != 1)) {
		fprintf(stderr, "%s: pool add/free counts %zu/%zu, expected 1/1\n",
		    name, pool_add_calls, pool_free_calls);
		return (-1);
	}
	if (queue_calls != 0) {
		fprintf(stderr, "%s: unexpected eviction-queue operations: %zu\n",
		    name, queue_calls);
		return (-1);
	}
	if (malloc_calls != 1) {
		fprintf(stderr, "%s: reading malloc count %zu, expected 1\n",
		    name, malloc_calls);
		return (-1);
	}
	if (readerlist_init_calls != expected_readerlist_inits) {
		fprintf(stderr, "%s: reader-list init count %zu, expected %zu\n",
		    name, readerlist_init_calls, expected_readerlist_inits);
		return (-1);
	}
	if (readerlist_free_calls != expected_readerlist_frees) {
		fprintf(stderr, "%s: reader-list free count %zu, expected %zu\n",
		    name, readerlist_free_calls, expected_readerlist_frees);
		return (-1);
	}
	if (request_get_calls != expected_requests) {
		fprintf(stderr, "%s: request-get count %zu, expected %zu\n",
		    name, request_get_calls, expected_requests);
		return (-1);
	}

	printf("PASS: %s\n", name);
	return (0);
}

int
main(void)
{

	if (run_case("reading allocation failure", FAIL_READING_ALLOC))
		return (1);
	if (run_case("reader-list initialization failure", FAIL_READERLIST_INIT))
		return (1);
	if (run_case("request-get startup failure", FAIL_REQUEST_GET))
		return (1);

	return (0);
}

/*
 * Regression for btree_node_dirty() allocation-failure unwinding.
 *
 * Compile the real btree_node.c and inject failures into the IMALLOCs which
 * run after the input node has been changed to SHADOW.  Each failed call must
 * return the input node to its original clean state, dirty parent, and lock
 * count before destroying the replacement node.
 */
#include <assert.h>
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

static struct node dirty_node;
static struct pool_elem dirty_elem;
static size_t alloc_calls;
static size_t fail_alloc_call;
static size_t pool_free_calls;
static size_t node_free_calls;
static size_t queue_calls;

static void * test_malloc(size_t);

#define malloc test_malloc
#include "../../kvlds/btree_node.c"
#undef malloc

static void *
test_malloc(size_t len)
{

	alloc_calls++;
	if ((fail_alloc_call != 0) && (alloc_calls == fail_alloc_call))
		return (NULL);
	return (malloc(len));
}

struct node *
node_alloc(uint64_t pagenum, uint64_t oldestleaf, uint32_t pagesize)
{

	memset(&dirty_node, 0, sizeof(dirty_node));
	memset(&dirty_elem, 0, sizeof(dirty_elem));
	dirty_node.pagenum = pagenum;
	dirty_node.oldestleaf = oldestleaf;
	dirty_node.oldestncleaf = oldestleaf;
	dirty_node.pagesize = pagesize;
	dirty_node.type = NODE_TYPE_NP;
	dirty_node.state = NODE_STATE_CLEAN;
	dirty_node.height = -1;
	dirty_node.nkeys = (size_t)(-1);
	return (&dirty_node);
}

void
node_free(struct node * N)
{

	assert(N == &dirty_node);
	node_free_calls++;
}

int
pool_rec_add(struct pool * P, void * rec, void ** evict)
{
	struct pool_elem ** E;

	assert(rec == &dirty_node);
	E = (struct pool_elem **)(void *)((uint8_t *)rec + P->offset);
	*E = &dirty_elem;
	dirty_elem.wire_count = 1;
	*evict = NULL;
	return (0);
}

void
pool_rec_free(struct pool * P, void * rec)
{
	struct pool_elem ** E;

	assert(rec == &dirty_node);
	E = (struct pool_elem **)(void *)((uint8_t *)rec + P->offset);
	assert(*E == &dirty_elem);
	assert(dirty_elem.wire_count == 1);
	pool_free_calls++;
	*E = NULL;
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
	return (-1);
}

void *
events_immediate_register(int (* callback)(void *), void * cookie, int prio)
{

	(void)callback;
	(void)cookie;
	(void)prio;
	return ((void *)(uintptr_t)1);
}

int
deserialize(struct node * N, const uint8_t * buf, size_t buflen)
{

	(void)N;
	(void)buf;
	(void)buflen;
	return (-1);
}

int
deserialize_root(struct btree * T, const uint8_t * buf)
{

	(void)T;
	(void)buf;
	return (-1);
}

struct elasticarray *
elasticarray_init(size_t nrec, size_t reclen)
{

	(void)nrec;
	(void)reclen;
	return (NULL);
}

int
elasticarray_resize(struct elasticarray * EA, size_t nrec, size_t reclen)
{

	(void)EA;
	(void)nrec;
	(void)reclen;
	return (-1);
}

size_t
elasticarray_getsize(const struct elasticarray * EA, size_t reclen)
{

	(void)EA;
	(void)reclen;
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
	return (-1);
}

void
elasticarray_shrink(struct elasticarray * EA, size_t nrec, size_t reclen)
{

	(void)EA;
	(void)nrec;
	(void)reclen;
}

int
elasticarray_truncate(struct elasticarray * EA)
{

	(void)EA;
	return (-1);
}

void *
elasticarray_get(struct elasticarray * EA, size_t pos, size_t reclen)
{

	(void)EA;
	(void)pos;
	(void)reclen;
	return (NULL);
}

void
elasticarray_free(struct elasticarray * EA)
{

	(void)EA;
}

int
elasticarray_export(struct elasticarray * EA, void ** buf, size_t * nrec,
    size_t reclen)
{

	(void)EA;
	(void)buf;
	(void)nrec;
	(void)reclen;
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
run_case(const char * name, unsigned int type, size_t fail_at)
{
	struct btree T;
	struct pool P;
	struct node N;
	struct node shadow_parent;
	struct node dirty_parent;
	struct pool_elem N_elem;
	struct kvpair_const pairs[2];
	const struct kvldskey * keys[2];
	struct node * children[3];
	struct node * result;
	size_t before;

	memset(&T, 0, sizeof(T));
	memset(&P, 0, sizeof(P));
	memset(&N, 0, sizeof(N));
	memset(&shadow_parent, 0, sizeof(shadow_parent));
	memset(&dirty_parent, 0, sizeof(dirty_parent));
	memset(&N_elem, 0, sizeof(N_elem));
	memset(pairs, 0, sizeof(pairs));
	memset(keys, 0, sizeof(keys));
	memset(children, 0, sizeof(children));

	P.offset = offsetof(struct node, pool_cookie);
	T.P = &P;

	shadow_parent.type = NODE_TYPE_PARENT;
	shadow_parent.state = NODE_STATE_CLEAN;
	dirty_parent.type = NODE_TYPE_PARENT;
	dirty_parent.state = NODE_STATE_DIRTY;

	N.type = type;
	N.state = NODE_STATE_CLEAN;
	N.root = 0;
	N.height = (type == NODE_TYPE_LEAF) ? 0 : 1;
	N.nkeys = 2;
	N.p_shadow = &shadow_parent;
	N.p_dirty = &dirty_parent;
	N.pool_cookie = &N_elem;
	N_elem.wire_count = 1;
	if (type == NODE_TYPE_LEAF)
		N.u.pairs = pairs;
	else {
		N.u.keys = keys;
		N.v.children = children;
	}

	alloc_calls = 0;
	fail_alloc_call = fail_at;
	pool_free_calls = 0;
	node_free_calls = 0;
	queue_calls = 0;
	before = N_elem.wire_count;

	result = btree_node_dirty(&T, &N);
	fail_alloc_call = 0;

	if (result != NULL) {
		fprintf(stderr, "%s: dirtying unexpectedly succeeded\n", name);
		return (-1);
	}
	if (N.state != NODE_STATE_CLEAN) {
		fprintf(stderr, "%s: input state was not restored\n", name);
		return (-1);
	}
	if (N.p_dirty != &dirty_parent) {
		fprintf(stderr, "%s: dirty parent was not restored\n", name);
		return (-1);
	}
	if (N_elem.wire_count != before) {
		fprintf(stderr, "%s: lock count %zu, expected %zu\n", name,
		    N_elem.wire_count, before);
		return (-1);
	}
	if ((pool_free_calls != 1) || (node_free_calls != 1)) {
		fprintf(stderr, "%s: replacement cleanup counts pool=%zu node=%zu\n",
		    name, pool_free_calls, node_free_calls);
		return (-1);
	}
	if (dirty_elem.wire_count != 1) {
		fprintf(stderr, "%s: replacement lock count changed to %zu\n",
		    name, dirty_elem.wire_count);
		return (-1);
	}
	if (queue_calls != 0) {
		fprintf(stderr, "%s: unexpected pool queue activity: %zu\n",
		    name, queue_calls);
		return (-1);
	}
	if (alloc_calls != fail_at) {
		fprintf(stderr, "%s: allocation count %zu, expected %zu\n",
		    name, alloc_calls, fail_at);
		return (-1);
	}

	printf("PASS: %s\n", name);
	return (0);
}

int
main(void)
{

	if (run_case("leaf pairs allocation unwind", NODE_TYPE_LEAF, 1))
		return (1);
	if (run_case("parent keys allocation unwind", NODE_TYPE_PARENT, 1))
		return (1);
	if (run_case("parent children allocation unwind", NODE_TYPE_PARENT, 2))
		return (1);

	return (0);
}

/*
 * Regression for findleaf() parent-lock unwinding when a child fetch cannot
 * be started.
 *
 * Compile the real btree_find.c.  A present parent points at a non-present
 * child and btree_node_fetch() is forced to fail synchronously.  Both public
 * search entry points must restore the parent's lock count to its baseline.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lib/datastruct/kvldskey.h"
#include "../../lib/datastruct/pool.h"
#include "../../kvlds/btree.h"
#include "../../kvlds/node.h"

static struct node * expected_fetch_node;
static size_t fetch_calls;
static size_t queue_adds;
static size_t queue_dels;

#include "../../kvlds/btree_find.c"

void
pool_addqueue(struct pool * P, void * rec)
{

	(void)P;
	(void)rec;
	queue_adds++;
}

void
pool_delqueue(struct pool * P, void * rec)
{

	(void)P;
	(void)rec;
	queue_dels++;
}

struct kvldskey *
kvldskey_create(const uint8_t * buf, size_t buflen)
{
	struct kvldskey * K;

	if (buflen > 255)
		return (NULL);
	if ((K = malloc(sizeof(struct kvldskey) + buflen)) == NULL)
		return (NULL);
	K->len = (uint8_t)buflen;
	if (buflen != 0)
		memcpy(K->buf, buf, buflen);

	return (K);
}

int
kvldskey_cmp2(const struct kvldskey * x, const struct kvldskey * y, size_t mlen)
{
	size_t i;
	size_t n;

	n = (x->len < y->len) ? x->len : y->len;
	for (i = mlen; i < n; i++) {
		if (x->buf[i] < y->buf[i])
			return (-1);
		if (x->buf[i] > y->buf[i])
			return (1);
	}
	if (x->len < y->len)
		return (-1);
	if (x->len > y->len)
		return (1);
	return (0);
}

int
btree_node_fetch(struct btree * T, struct node * N,
    int (* callback)(void *), void * cookie)
{

	(void)T;
	(void)callback;
	(void)cookie;
	assert(N == expected_fetch_node);
	fetch_calls++;

	/* Simulate a synchronous failure to start the child fetch. */
	return (-1);
}

static int
unexpected_leaf_callback(void * cookie, struct node * N)
{

	(void)cookie;
	(void)N;
	assert(0 && "leaf callback unexpectedly called");
	return (-1);
}

static int
unexpected_range_callback(void * cookie, struct node * N,
    struct kvldskey * endpoint)
{

	(void)cookie;
	(void)N;
	(void)endpoint;
	assert(0 && "range callback unexpectedly called");
	return (-1);
}

static void
setup_tree(struct btree * T, struct pool * P, struct node * parent,
    struct node * child, struct pool_elem * parent_elem,
    struct node ** children)
{

	memset(T, 0, sizeof(*T));
	memset(P, 0, sizeof(*P));
	memset(parent, 0, sizeof(*parent));
	memset(child, 0, sizeof(*child));
	memset(parent_elem, 0, sizeof(*parent_elem));

	P->offset = offsetof(struct node, pool_cookie);
	T->P = P;

	parent->type = NODE_TYPE_PARENT;
	parent->state = NODE_STATE_CLEAN;
	parent->height = 1;
	parent->nkeys = 0;
	parent->pool_cookie = parent_elem;
	parent_elem->wire_count = 1;
	parent->v.children = children;

	child->type = NODE_TYPE_NP;
	child->state = NODE_STATE_CLEAN;
	child->height = -1;
	child->nkeys = (size_t)-1;
	child->pool_cookie = NULL;
	children[0] = child;

	expected_fetch_node = child;
	fetch_calls = 0;
	queue_adds = 0;
	queue_dels = 0;
}

static int
check_result(const char * name, int rc, struct node * child,
    const struct pool_elem * parent_elem)
{

	if (rc != -1) {
		fprintf(stderr, "%s: search unexpectedly returned %d\n", name, rc);
		return (-1);
	}
	if (fetch_calls != 1) {
		fprintf(stderr, "%s: fetch call count %zu, expected 1\n",
		    name, fetch_calls);
		return (-1);
	}
	if (parent_elem->wire_count != 1) {
		fprintf(stderr,
		    "%s: parent lock count %zu after failed fetch, expected 1\n",
		    name, parent_elem->wire_count);
		return (-1);
	}
	if (child->type != NODE_TYPE_NP) {
		fprintf(stderr, "%s: child type changed to %u\n",
		    name, child->type);
		return (-1);
	}
	if ((queue_adds != 0) || (queue_dels != 0)) {
		fprintf(stderr,
		    "%s: unexpected eviction-queue operations: add=%zu del=%zu\n",
		    name, queue_adds, queue_dels);
		return (-1);
	}

	printf("PASS: %s\n", name);
	return (0);
}

static int
run_leaf_case(void)
{
	struct btree T;
	struct pool P;
	struct node parent;
	struct node child;
	struct pool_elem parent_elem;
	struct node * children[1];
	struct kvldskey key = {0};
	int rc;

	setup_tree(&T, &P, &parent, &child, &parent_elem, children);
	rc = btree_find_leaf(&T, &parent, &key,
	    unexpected_leaf_callback, NULL);

	return (check_result("btree_find_leaf fetch-start failure",
	    rc, &child, &parent_elem));
}

static int
run_range_case(void)
{
	struct btree T;
	struct pool P;
	struct node parent;
	struct node child;
	struct pool_elem parent_elem;
	struct node * children[1];
	struct kvldskey key = {0};
	int rc;

	setup_tree(&T, &P, &parent, &child, &parent_elem, children);
	rc = btree_find_range(&T, &parent, &key, 0,
	    unexpected_range_callback, NULL);

	return (check_result("btree_find_range fetch-start failure",
	    rc, &child, &parent_elem));
}

int
main(void)
{

	if (run_leaf_case())
		return (1);
	if (run_range_case())
		return (1);
	return (0);
}

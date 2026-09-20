#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kvlds/btree_balance.c"

static struct node old_root;
static struct node new_root;
static struct pool_elem old_root_elem;
static struct pool_elem new_root_elem;
static struct node * expected_old_root;
static struct node * expected_new_root;
static int split_called;
static int destroy_called;
static size_t destroy_wire_count;

void
pool_addqueue(struct pool * P, void * rec)
{
	(void)P;
	(void)rec;
}

void
pool_delqueue(struct pool * P, void * rec)
{
	(void)P;
	(void)rec;
}

void *
events_immediate_register(int (* callback)(void *), void * cookie, int prio)
{
	(void)callback;
	(void)cookie;
	(void)prio;
	return ((void *)(uintptr_t)1);
}

size_t
serialize_size(struct node * N)
{
	(void)N;
	return (0);
}

size_t
serialize_merge_size(struct node * N)
{
	(void)N;
	return (0);
}

struct node *
btree_node_mknode(struct btree * T, unsigned int type, int height,
    size_t nkeys, const struct kvldskey ** keys, struct node ** children,
    struct kvpair_const * pairs)
{
	(void)T;
	(void)pairs;
	memset(&new_root, 0, sizeof(new_root));
	memset(&new_root_elem, 0, sizeof(new_root_elem));
	new_root.type = type;
	new_root.state = NODE_STATE_DIRTY;
	new_root.height = (int8_t)height;
	new_root.nkeys = nkeys;
	new_root.u.keys = keys;
	new_root.v.children = children;
	new_root.pool_cookie = &new_root_elem;
	new_root_elem.wire_count = 1;
	expected_new_root = &new_root;
	return (&new_root);
}

void
btree_node_destroy(struct btree * T, struct node * N)
{
	(void)T;
	destroy_called++;
	if (N != expected_new_root) {
		fprintf(stderr, "destroyed unexpected node\n");
		return;
	}
	destroy_wire_count = new_root_elem.wire_count;
	free(N->u.keys);
	free(N->v.children);
	N->u.keys = NULL;
	N->v.children = NULL;
}

int
btree_node_fetch(struct btree * T, struct node * N,
    int (* callback)(void *), void * cookie)
{
	(void)T;
	(void)N;
	(void)callback;
	(void)cookie;
	return (0);
}

int
btree_node_descend(struct btree * T, struct node * N,
    int (* callback)(void *, struct node *), void * cookie)
{
	(void)T;
	(void)N;
	(void)callback;
	(void)cookie;
	return (0);
}

struct node *
btree_node_dirty(struct btree * T, struct node * N)
{
	(void)T;
	return (N);
}

size_t
btree_node_split_nparts(struct btree * T, struct node * N)
{
	(void)T;
	if (N != expected_old_root) {
		fprintf(stderr, "split_nparts saw unexpected root\n");
		exit(2);
	}
	return (2);
}

int
btree_node_split(struct btree * T, struct node * N,
    const struct kvldskey ** keys, struct node ** children, size_t * nparts)
{
	(void)keys;
	(void)children;
	(void)nparts;
	split_called++;
	if (N != expected_old_root) {
		fprintf(stderr, "split saw unexpected old root\n");
		exit(2);
	}
	if (expected_new_root == NULL) {
		fprintf(stderr, "split ran before parent creation\n");
		exit(2);
	}
	if (N->root != 0 || N->p_dirty != expected_new_root) {
		fprintf(stderr, "forward splitroot state was not established\n");
		exit(2);
	}
	if (new_root_elem.wire_count != 3) {
		fprintf(stderr, "new root lock count before failure: %zu, expected 3\n",
		    new_root_elem.wire_count);
		exit(2);
	}
	if (T->nnodes != 18) {
		fprintf(stderr, "nnodes before injected failure: %ju, expected 18\n",
		    (uintmax_t)T->nnodes);
		exit(2);
	}
	return (-1);
}

int
btree_node_merge(struct btree * T, struct node ** children_in,
    const struct kvldskey ** keys_in, struct node ** children_out,
    const struct kvldskey ** keys_out, size_t nsep)
{
	(void)T;
	(void)children_in;
	(void)keys_in;
	(void)children_out;
	(void)keys_out;
	(void)nsep;
	return (0);
}

static int
check_int(const char * name, uintmax_t got, uintmax_t want)
{
	if (got == want)
		return (0);
	fprintf(stderr, "%s: got %ju, expected %ju\n", name, got, want);
	return (-1);
}

int
main(void)
{
	struct btree T;
	struct pool P;
	int failed = 0;

	memset(&T, 0, sizeof(T));
	memset(&P, 0, sizeof(P));
	memset(&old_root, 0, sizeof(old_root));
	memset(&old_root_elem, 0, sizeof(old_root_elem));

	P.offset = offsetof(struct node, pool_cookie);
	T.P = &P;
	T.nnodes = 17;

	old_root.type = NODE_TYPE_LEAF;
	old_root.state = NODE_STATE_DIRTY;
	old_root.root = 1;
	old_root.height = 0;
	old_root.nkeys = 0;
	old_root.pool_cookie = &old_root_elem;
	old_root_elem.wire_count = 2;
	expected_old_root = &old_root;

	if (splitroot(&T, &old_root) != NULL) {
		fprintf(stderr, "splitroot unexpectedly succeeded\n");
		return (1);
	}

	failed |= (check_int("split calls", (uintmax_t)split_called, 1) != 0);
	failed |= (check_int("destroy calls", (uintmax_t)destroy_called, 1) != 0);
	failed |= (check_int("old root marker", (uintmax_t)old_root.root, 1) != 0);
	failed |= (check_int("old root lock count",
	    (uintmax_t)old_root_elem.wire_count, 2) != 0);
	failed |= (check_int("new root destroy lock count",
	    (uintmax_t)destroy_wire_count, 1) != 0);
	failed |= (check_int("tree node count", (uintmax_t)T.nnodes, 17) != 0);

	if (old_root.p_dirty != NULL) {
		fprintf(stderr, "old root still points at failed parent\n");
		failed = 1;
	}

	if (failed)
		return (1);

	puts("splitroot unwind regression: ok");
	return (0);
}

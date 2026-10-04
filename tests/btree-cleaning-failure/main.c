/*
 * Regressions for callback_clean() abandonment and failure accounting.
 *
 * Compile the real btree_cleaning.c and force allocation of the per-node
 * cleaning state to fail.  The abandoned node must release its pending clean,
 * unlock exactly once, and free the now-empty cleaning group.  Also exercise
 * normal SHADOW-node abandonment through the real cleaner shutdown function.
 */
#include <sys/time.h>

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../kvlds/btree.h"
#include "../../kvlds/node.h"

static size_t unlock_calls;
static size_t free_calls;
static size_t timer_cancel_calls;
static size_t event_calls;
static int fail_alloc;

static void * test_malloc(size_t);
static void test_free(void *);

/*
 * btree_cleaning.c only needs these three btree_node operations.  Suppress
 * btree_node.h so the fixture can provide deterministic stand-ins without a
 * page pool.
 */
#define BTREE_NODE_H_
static void btree_node_unlock(struct btree *, struct node *);
static int btree_node_descend(struct btree *, struct node *,
    int (*)(void *, struct node *), void *);
static struct node * btree_node_dirty(struct btree *, struct node *);

#define malloc test_malloc
#define free test_free
#include "../../kvlds/btree_cleaning.c"
#undef free
#undef malloc

static void *
test_malloc(size_t len)
{

	if (fail_alloc)
		return (NULL);
	return (malloc(len));
}

static void
test_free(void * ptr)
{

	free_calls++;
	free(ptr);
}

static void
btree_node_unlock(struct btree * T, struct node * N)
{

	assert(T != NULL);
	assert(N != NULL);
	unlock_calls++;
}

static int
btree_node_descend(struct btree * T, struct node * N,
    int (* callback)(void *, struct node *), void * cookie)
{

	(void)T;
	(void)N;
	(void)callback;
	(void)cookie;
	return (-1);
}

static struct node *
btree_node_dirty(struct btree * T, struct node * N)
{

	(void)T;
	(void)N;
	return (NULL);
}

void *
events_timer_register(int (* callback)(void *), void * cookie,
    const struct timeval * timeout)
{

	(void)callback;
	(void)cookie;
	(void)timeout;
	return ((void *)1);
}

void
events_timer_cancel(void * cookie)
{

	(void)cookie;
	timer_cancel_calls++;
}

int
events_run(void)
{

	event_calls++;
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

static int
allocation_failure(void)
{
	struct btree T = {0};
	struct cleaner C = {0};
	struct node N = {0};
	struct cleaning_group * CG;

	if ((CG = calloc(1, sizeof(*CG))) == NULL)
		return (1);

	C.T = &T;
	C.head = CG;
	C.pending_cleans = 1;

	CG->C = &C;
	CG->pending_fetches = 1;

	N.type = NODE_TYPE_LEAF;
	N.state = NODE_STATE_CLEAN;

	fail_alloc = 1;
	assert(callback_clean(CG, &N) == -1);
	fail_alloc = 0;

	assert(C.pending_cleans == 0);
	assert(C.head == NULL);
	assert(unlock_calls == 1);
	assert(free_calls == 1);

	puts("PASS: callback_clean failure releases pending clean and group");
	return (0);
}


static void
reset_counts(void)
{

	unlock_calls = 0;
	free_calls = 0;
	timer_cancel_calls = 0;
	event_calls = 0;
	fail_alloc = 0;
}

/* Add the state of a group whose leaf callbacks have not run yet. */
static struct cleaning_group *
pending_group(struct cleaner * C, size_t count)
{
	struct cleaning_group * CG;

	if ((CG = calloc(1, sizeof(*CG))) == NULL)
		return (NULL);
	CG->C = C;
	CG->pending_fetches = count;
	CG->next = C->head;
	if (CG->next != NULL)
		CG->next->prev = CG;
	C->head = CG;
	C->pending_cleans += count;
	return (CG);
}

/*
 * A leaf can become SHADOW while a previously queued cleaner callback waits.
 * No allocation is forced to fail here.  Let the production shutdown assertion,
 * not an earlier fixture assertion on C->head, expose an orphaned empty group.
 */
static int
shadow_shutdown(void)
{
	struct btree T = {0};
	struct node A = {0};
	struct node B = {0};
	struct cleaner * C;
	struct cleaning_group * CG;

	if ((C = btree_cleaning_start(&T, 1.0)) == NULL)
		return (1);
	if ((CG = pending_group(C, 2)) == NULL)
		return (1);
	A.type = B.type = NODE_TYPE_LEAF;
	A.state = B.state = NODE_STATE_SHADOW;

	assert(callback_clean(CG, &A) == 0);
	assert(C->head == CG);
	assert(CG->pending_fetches == 1);
	assert(C->pending_cleans == 1);
	assert(free_calls == 0);
	assert(callback_clean(CG, &B) == 0);
	assert(C->pending_cleans == 0);
	assert(unlock_calls == 2);
	assert(fail_alloc == 0);

	printf("shadow shutdown: pending=%zu, group_pending=%d, group_left=%d\n",
	    C->pending_cleans, C->group_pending, C->head != NULL);
	fflush(stdout);
	btree_cleaning_stop(C);
	assert(free_calls == 2);
	assert(timer_cancel_calls == 1);
	assert(event_calls == 0);
	puts("PASS: shadow callbacks retain pending sibling and allow shutdown");
	return (0);
}

/* A completed clean sibling must survive abandonment of the final fetch. */
static int
clean_sibling(void)
{
	struct btree T = {0};
	struct node clean = {0};
	struct node shadow = {0};
	struct cleaner * C;
	struct cleaning_group * CG;

	T.nextblk = T.npages = 64;
	if ((C = btree_cleaning_start(&T, 1.0)) == NULL)
		return (1);
	if ((CG = pending_group(C, 2)) == NULL)
		return (1);
	clean.type = shadow.type = NODE_TYPE_LEAF;
	clean.state = NODE_STATE_CLEAN;
	shadow.state = NODE_STATE_SHADOW;

	assert(callback_clean(CG, &clean) == 0);
	assert(callback_clean(CG, &shadow) == 0);
	assert(C->head == CG);
	assert(CG->head == clean.v.cstate);
	assert(clean.v.cstate != NULL);
	assert(CG->pending_fetches == 0);
	assert(C->pending_cleans == 1);
	assert(free_calls == 0);
	assert(btree_cleaning_possible(C) != 0);

	btree_cleaning_notify_dirtying(C, &clean);
	assert(clean.v.cstate == NULL);
	assert(C->pending_cleans == 0);
	assert(C->head == NULL);
	assert(unlock_calls == 2);
	btree_cleaning_stop(C);
	assert(free_calls == 3);
	assert(timer_cancel_calls == 1);
	assert(event_calls == 0);
	puts("PASS: clean sibling remains owned until normal dirty notification");
	return (0);
}

/* Completing a non-head group must keep the other group's links valid. */
static int
other_group(void)
{
	struct btree T = {0};
	struct node A = {0};
	struct node B = {0};
	struct cleaner * C;
	struct cleaning_group * older;
	struct cleaning_group * newer;

	if ((C = btree_cleaning_start(&T, 1.0)) == NULL)
		return (1);
	if ((older = pending_group(C, 1)) == NULL)
		return (1);
	if ((newer = pending_group(C, 1)) == NULL)
		return (1);
	A.type = B.type = NODE_TYPE_LEAF;
	A.state = B.state = NODE_STATE_SHADOW;

	assert(callback_clean(older, &A) == 0);
	assert(C->head == newer);
	assert(newer->prev == NULL);
	assert(newer->next == NULL);
	assert(C->pending_cleans == 1);
	assert(free_calls == 1);
	assert(callback_clean(newer, &B) == 0);
	assert(C->head == NULL);
	assert(C->pending_cleans == 0);
	btree_cleaning_stop(C);
	assert(unlock_calls == 2);
	assert(free_calls == 3);
	assert(timer_cancel_calls == 1);
	assert(event_calls == 0);
	puts("PASS: non-head group completion preserves sibling links");
	return (0);
}

int
main(void)
{

	reset_counts();
	if (shadow_shutdown())
		return (1);
	reset_counts();
	if (clean_sibling())
		return (1);
	reset_counts();
	if (other_group())
		return (1);
	reset_counts();
	return (allocation_failure());
}

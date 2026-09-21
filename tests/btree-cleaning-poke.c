#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Exercise poke() and free_cg() from the real implementation while forcing
 * the one operation whose failure triggers the #336 unwind.
 */
#define btree_node_descend test_btree_node_descend
#include "../kvlds/btree_cleaning.c"
#undef btree_node_descend

int
test_btree_node_descend(struct btree * T, struct node * N,
    int (*callback)(void *, struct node *), void * cookie)
{
	(void)T;
	(void)N;
	(void)callback;
	(void)cookie;

	/* Simulate failure to start the descent after poke() linked its group. */
	return (-1);
}

int
main(void)
{
	struct btree T = {0};
	struct cleaner C = {0};
	struct cleaning_group * A;

	/*
	 * Start with an existing group.  poke() will insert a temporary group
	 * B before A, making A->prev point at B, then our descend stub fails.
	 */
	if ((A = calloc(1, sizeof(*A))) == NULL)
		return (1);
	T.poolsz = 32;
	T.root_shadow = (struct node *)(uintptr_t)1;
	C.T = &T;
	C.cleandebt = 1.0;
	C.head = A;
	A->C = &C;

	assert(poke(&C) == -1);

	/*
	 * The failed insertion must be fully rolled back.  In the old code
	 * A->prev still points at the just-freed B; free_cg(A) then writes
	 * through that freed pointer and leaves C.head dangling.
	 */
	assert(C.head == A);
	assert(A->prev == NULL);
	assert(C.group_pending == 0);

	free_cg(A);
	assert(C.head == NULL);

	puts("PASS: failed cleaning-group insertion restores both list links");
	return (0);
}

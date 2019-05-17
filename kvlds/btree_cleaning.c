#include <sys/time.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "events.h"
#include "warnp.h"

#include "btree.h"
#include "btree_node.h"
#include "node.h"

#include "btree_cleaning.h"

/* Cleaning state for a single node. */
struct cleaning {
	struct node * N;		/* The node itself. */
	struct cleaning_group * G;	/* Group to which this belongs. */
	struct cleaning * next;		/* Next in group. */
	struct cleaning * prev;		/* Previous in group. */
};

/* Cleaning state for a group. */
struct cleaning_group {
	struct cleaner * C;	/* The cleaner. */
	struct cleaning * head;	/* Head of the stack of cleanable nodes. */
	struct cleaning_group * next;	/* Next group. */
	struct cleaning_group * prev;	/* Previous group. */
	size_t pending_fetches;	/* Number of nodes not fetched yet. */
};

/* Cleaner state. */
struct cleaner {
	struct btree * T;	/* The tree. */
	double cleanrate;	/* Cleans per second per block of garbage. */
	double cleandebt;	/* How many cleans should we do? */
	void * cleantimer;	/* Cookie returned by events_timer. */
	int group_pending;	/* Are we trying to find a group to clean? */
	struct cleaning_group * head;	/* Head of the list of groups. */
	size_t pending_cleans;	/* Number of nodes fetching + waiting. */
};

/* Time between ticks of the cleaning debt clock. */
static const struct timeval onesec = {.tv_sec = 1, .tv_usec = 0};

static int poke(struct cleaner *);

/* Compute oldestncleaf upwards in the shadow tree. */
static void
recompute_oncl(struct node * N)
{
	size_t i;

	/* If we've gone past the top of the tree, do nothing. */
	if (N == NULL)
		return;

	/* Find the lowest value. */
	N->oldestncleaf = (uint64_t)(-1);
	for (i = 0; i <= N->nkeys; i++) {
		if (N->v.children[i]->oldestncleaf < N->oldestncleaf)
			N->oldestncleaf = N->v.children[i]->oldestncleaf;
	}

	/* Move up the tree. */
	recompute_oncl(N->p_shadow);
}

/* Unlink and free a cleaning group. */
static void
free_cg(struct cleaning_group * CG)
{
	struct cleaner * C = CG->C;

	/* Sanity-check: The group must have no nodes. */
	assert(CG->head == NULL);
	assert(CG->pending_fetches == 0);

	/* Remove from the list of groups. */
	if (CG->prev != NULL)
		CG->prev->next = CG->next;
	else
		C->head = CG->next;
	if (CG->next != NULL)
		CG->next->prev = CG->prev;

	/* Free the group. */
	free(CG);
}

/* Unlink a node from a cleaning group. */
static void
free_cstate(struct cleaning * C)
{
	struct cleaning_group * CG = C->G;

	/* Remove from the list of nodes being cleaned. */
	if (C->prev != NULL)
		C->prev->next = C->next;
	else
		CG->head = C->next;
	if (C->next != NULL)
		C->next->prev = C->prev;

	/* Mark the node as no longer being cleaned. */
	C->N->v.cstate = NULL;

	/* Release node lock held by the cleaner. */
	btree_node_unlock(C->G->C->T, C->N);

	/* This node is no longer pending cleaning. */
	C->G->C->pending_cleans--;

	/* Kill the group if it is now empty. */
	if ((CG->head == NULL) && (CG->pending_fetches == 0))
		free_cg(CG);

	/* Free the node-cleaning state. */
	free(C);
}

/* Add a node to a cleaning group. */
static int
callback_clean(void * cookie, struct node * N)
{
	struct cleaning_group * CG = cookie;
	struct cleaning * C;

	/* This must be a leaf node. */
	assert(N->type == NODE_TYPE_LEAF);

	/* We're not fetching this node any more. */
	CG->pending_fetches--;

	/* If this node is not CLEAN, we don't need to clean it any more. */
	if (N->state != NODE_STATE_CLEAN) {
		CG->C->pending_cleans--;
		btree_node_unlock(CG->C->T, N);
		goto done;
	}

	/* Bake a cookie. */
	if ((C = malloc(sizeof(struct cleaning))) == NULL)
		goto err1;
	C->N = N;
	C->G = CG;

	/* Hook this node into the group. */
	C->next = CG->head;
	C->prev = NULL;
	CG->head = C;
	if (C->next != NULL)
		C->next->prev = C;

	/* Mark this node as pending cleaning. */
	N->v.cstate = C;

done:
	/* Success! */
	return (0);

err1:
	btree_node_unlock(CG->C->T, N);

	/* Failure! */
	return (-1);
}

/* Find a group of nodes to clean. */
static int
callback_find(void * cookie, struct node * N)
{
	struct cleaning_group * CG = cookie;
	struct cleaner * C = CG->C;
	size_t i;
	int repoke = 1;

	/*
	 * We're no longer trying to find a group to clean (unless we decide
	 * to descend into another node, in which case we're still looking
	 * for the right group of nodes).  We're not fetching a node any more
	 * (unless we start fetching more nodes).
	 */
	C->group_pending = 0;
	CG->pending_fetches--;

	/*
	 * If there aren't any old leaves under this node which aren't
	 * already being cleaned, we have nothing to do except free the
	 * cleaner group.  This can happen if we have a small tree and are
	 * cleaning it very aggressively.
	 */
	if (N->oldestncleaf >= C->T->nextblk - C->T->nnodes / 2) {
		/* Release the cleaner group. */
		free_cg(CG);

		/*
		 * No need to poke the cleaner again quite yet; there isn't
		 * any useful cleaning to be done right now.
		 */
		repoke = 0;

		/* That's all. */
		goto done;
	}

	/* If we have a node of height > 1, descend further. */
	if (N->height > 1) {
		/* Find the node we need to descend into. */
		for (i = 0; i <= N->nkeys; i++) {
			if (N->oldestncleaf ==
			    N->v.children[i]->oldestncleaf) {
				/* This is where the next clean happens. */
				C->group_pending = 1;
				CG->pending_fetches++;
				if (btree_node_descend(C->T, N->v.children[i],
				    callback_find, CG))
					goto err1;
				break;
			}
		}

		/* We should have found a node. */
		if (i > N->nkeys) {
			warn0("Node has oldestncleaf not matching"
			    " any of its children!");
			goto err1;
		}

		/* That's all we need to do. */
		goto done;
	}

	/* If we have a node of height 1, figure out which leaves to clean. */
	if (N->height == 1) {
		/* Look for nodes with low oldestncleaf values. */
		for (i = 0; i <= N->nkeys; i++) {
			if (N->v.children[i]->oldestncleaf <
			    C->T->nextblk - C->T->nnodes / 2) {
				/* This child needs to be cleaned. */
				CG->pending_fetches++;
				C->pending_cleans++;
				N->v.children[i]->oldestncleaf =
				    (uint64_t)(-1);
				if (btree_node_descend(C->T, N->v.children[i],
				    callback_clean, CG))
					goto err1;
			}
		}

		/* We should have found at least one child to clean. */
		assert(CG->pending_fetches);

		/* Recompute oldestncleaf upwards. */
		recompute_oncl(N);

		/* That's all we need to do. */
		goto done;
	}

	/* This node needs to be cleaned. */
	CG->pending_fetches++;
	C->pending_cleans++;
	N->oldestncleaf = (uint64_t)(-1);
	if (btree_node_descend(C->T, N, callback_clean, CG))
		goto err1;

	/* Recompute oldestncleaf upwards. */
	recompute_oncl(N->p_shadow);

done:
	/* Unlock the node. */
	btree_node_unlock(C->T, N);

	/* Launch cleaning if possible and appropriate. */
	if (repoke && poke(C))
		goto err0;

	/* Success! */
	return (0);

err1:
	btree_node_unlock(C->T, N);
err0:
	/* Failure! */
	return (-1);
}

/* Launch cleaning if possible and appropriate. */
static int
poke(struct cleaner * C)
{
	struct cleaning_group * CG;

	/*
	 * If we're trying to find a group to clean, we need to wait until
	 * that is done before we can look for another group.
	 */
	if (C->group_pending)
		goto done;

	/*
	 * If we're using more than 1/16 of our memory to hold pages which
	 * are being cleaned, stop there; that's plenty.
	 */
	if (C->pending_cleans > C->T->poolsz / 16)
		goto done;

	/*
	 * If the number of nodes we have waiting to be fetched or dirtied
	 * is more than the cleaning debt, we don't need to look for any more
	 * pages to clean yet.
	 */
	if (C->pending_cleans >= C->cleandebt)
		goto done;

	/* We're going to launch a group of node cleans. */
	if ((CG = malloc(sizeof(struct cleaning_group))) == NULL)
		goto err0;
	CG->C = C;
	CG->head = NULL;
	CG->pending_fetches = 1;
	C->group_pending = 1;

	/* Hook this group into the list of groups. */
	CG->next = C->head;
	CG->prev = NULL;
	C->head = CG;
	if (CG->next != NULL)
		CG->next->prev = CG;

	/* Find the right group to clean. */
	if (btree_node_descend(C->T, C->T->root_shadow, callback_find, CG))
		goto err1;

done:
	/* Success! */
	return (0);

err1:
	C->head = CG->next;
	C->group_pending = 0;
	free(CG);
err0:
	/* Failure! */
	return (-1);
}

/* Cleaning timer tick. */
static int
tick(void * cookie)
{
	struct cleaner * C = cookie;
	struct btree * T = C->T;

	/* The timer is not running. */
	C->cleantimer = NULL;

	/*
	 * Adjust our "cleaning debt" based on current amount of garbage.
	 * This is an underestimate of the amount of garbage if page have
	 * been split and not yet synced, since nnodes reflects the current
	 * size of the dirty tree while npages reflects the on-disk storage
	 * used.  The if() eliminates potential arithmetic underflow in such
	 * circumstances.
	 */
	if (T->npages >= T->nnodes)
		C->cleandebt += (T->npages - T->nnodes) * C->cleanrate;

	/**
	 * Limit our "cleaning balance" based on the size of the tree.  We
	 * allow the "cleaning debt" to be negative on the basis that if we
	 * have been dirtying lots of pages, we should wait for more than a
	 * momentary pause before we start cleaning; but if the debt is a
	 * negative value which is large compared to the size of the tree,
	 * it suggests that we have just finished doing a large batch of
	 * deletes (otherwise it's very difficult to dirty more pages than
	 * the tree contains), in which case we should be more aggressive.
	 *
	 * Conversely, having a cleaning debt which is larger than the size
	 * of the tree is absurd, since it will cause us to re-clean recently
	 * cleaned nodes even if the tree is now completely compact; so don't
	 * allow that either.
	 */
	if (C->cleandebt + T->nnodes < 0)
		C->cleandebt = -(double)(T->nnodes);
	if (C->cleandebt > T->nnodes)
		C->cleandebt = T->nnodes;

	/* Launch cleaning if possible and appropriate. */
	if (poke(C))
		goto err0;

	/* Schedule the next timer tick. */
	if ((C->cleantimer =
	    events_timer_register(tick, C, &onesec)) == NULL) {
		warnp("events_timer_register");
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_cleaning_start(T, Scost):
 * Launch background cleaning of the B+Tree ${T}.  Attempt to minimize the
 * cost of storage plus I/O, based on a GB-month of storage costing ${Scost}
 * time as much as 10^6 I/Os.  Return a cookie which can be passed to
 * clean_stop to stop background cleaning.
 */
struct cleaner *
btree_cleaning_start(struct btree * T, double Scost)
{
	struct cleaner * C;

	(void)Scost;

	/* Create a cleaner state structure. */
	if ((C = malloc(sizeof(struct cleaner))) == NULL)
		goto err0;
	C->T = T;
	C->group_pending = 0;
	C->head = NULL;
	C->pending_cleans = 0;

	/**
	 * The optimal rate of cleaning is when the cost accrued to store
	 * garbage (inaccessible pages) is equal to the cost accrued to
	 * cleaning (both deliberate and the cleaning which occurs as a side
	 * effect of modifying operations).
	 *
	 * Based on the page size, the ratio of storage cost to I/O cost, and
	 * conversion factors, we can compute a factor which tells us how
	 * many I/Os we should perform per second per block of garbage.
	 */
	C->cleanrate = (T->pagelen / 1000000000.) *	/* GB per page. */
	    (1.0 / 86400. / 30.) *			/* months per s. */
	    Scost *				/* 10^6 I/Os per GB-month. */
	    1000000.;				/* I/Os per (10^6 I/Os). */

	/**
	 * Every second we will look at how much garbage we have and increase
	 * our "cleaning debt"; when this value gets too large, we'll make a
	 * "payment" on the debt by performing some cleaning.
	 */
	C->cleandebt = 0;
	if ((C->cleantimer =
	    events_timer_register(tick, C, &onesec)) == NULL) {
		warnp("events_timer_register");
		goto err1;
	}

	/* Success! */
	return (C);

err1:
	free(C);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * btree_cleaning_notify_dirtying(C, N):
 * Notify the cleaner that a page is being dirtied.
 */
void
btree_cleaning_notify_dirtying(struct cleaner * C, struct node * N)
{
	struct btree * T = C->T;

	/*
	 * Adjust our "cleaning debt" based on this page.  We count a page
	 * which is x% of the maximum age as being x% of a page-cleaning;
	 * this is based on the arbitrary assumption that recently modified
	 * pages are somewhat more likely to be modified again, but not
	 * dramatically so.
	 */
	C->cleandebt -= (T->nextblk - N->pagenum) / (double)(T->npages);

	/*
	 * If this node is waiting to be dirtied by the cleaner, remove it
	 * from the queue.
	 */
	if ((N->type == NODE_TYPE_LEAF) && (N->state == NODE_STATE_CLEAN) &&
	    (N->v.cstate != NULL)) {
		/* Remove the node from the cleaning group. */
		free_cstate(N->v.cstate);
	}
}

/**
 * btree_cleaning_possible(C):
 * Return non-zero if the cleaner has any groups of pages fetched which it
 * is waiting for an opportunity to dirty.
 */
int
btree_cleaning_possible(struct cleaner * C)
{
	struct cleaning_group * G;

	/* Scan through the list of groups. */
	for (G = C->head; G != NULL; G = G->next) {
		/* Is this group ready for cleaning? */
		if (G->pending_fetches == 0)
			return (1);
	}

	/* No groups are ready for cleaning. */
	return (0);
}

/**
 * btree_cleaning_clean(C):
 * Dirty whatever pages the cleaner wants to dirty.
 */
int
btree_cleaning_clean(struct cleaner * C)
{
	struct cleaning_group * G;
	struct cleaning_group * Gnext;
	struct cleaning * CC;
	struct cleaning * CCnext;

	/* Scan through the list of groups. */
	for (G = C->head; G != NULL; G = Gnext) {
		/* Record the next group, since G will be freed. */
		Gnext = G->next;

		/* Is this group ready for cleaning? */
		if (G->pending_fetches != 0)
			continue;

		/* Scan through the list of cleaning structures. */
		for (CC = G->head; CC != NULL; CC = CCnext) {
			/* Record the next struct, since CC will be freed. */
			CCnext = CC->next;

			/* Dirty the node. */
			if (btree_node_dirty(C->T, CC->N) == 0)
				goto err0;
		}
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * btree_cleaning_stop(C):
 * Stop the background cleaning for which the cookie ${C} was returned by
 * clean_start.
 */
void
btree_cleaning_stop(struct cleaner * C)
{

	/* Stop the timer if it is running. */
	if (C->cleantimer != NULL) {
		events_timer_cancel(C->cleantimer);
		C->cleantimer = NULL;
	}

	/* Loop until we have no cleaning pending. */
	do {
		/* If we have nodes ready to be dirtied, dirty them. */
		if (btree_cleaning_clean(C)) {
			warnp("Failure dirtying nodes in cleaner");
			exit(1);
		}

		/* Do we have any cleaning groups left? */
		if ((C->group_pending == 0) && (C->pending_cleans == 0))
			break;

		/* Run any available events. */
		if (events_run()) {
			warnp("Error running event loop");
			exit(1);
		}
	} while (1);

	/* We should have no cleaning groups. */
	assert(C->head == NULL);

	/* Free the cleaner state structure. */
	free(C);
}

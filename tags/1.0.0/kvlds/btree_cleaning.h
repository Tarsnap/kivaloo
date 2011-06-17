#ifndef _BTREE_CLEANING_H_
#define _BTREE_CLEANING_H_

/* Opaque types. */
struct btree;
struct cleaner;
struct node;

/**
 * btree_cleaning_start(T, Scost):
 * Launch background cleaning of the B+Tree ${T}.  Attempt to minimize the
 * cost of storage plus I/O, based on a GB-month of storage costing ${Scost}
 * time as much as 10^6 I/Os.  Return a cookie which can be passed to
 * clean_stop to stop background cleaning.
 */
struct cleaner * btree_cleaning_start(struct btree *, double);

/**
 * btree_cleaning_notify_dirtying(C, N):
 * Notify the cleaner that a page is being dirtied.
 */
void btree_cleaning_notify_dirtying(struct cleaner *, struct node *);

/**
 * btree_cleaning_possible(C):
 * Return non-zero if the cleaner has any groups of pages fetched which it
 * is waiting for an opportunity to dirty.
 */
int btree_cleaning_possible(struct cleaner *);

/**
 * btree_cleaning_clean(C):
 * Dirty whatever pages the cleaner wants to dirty.
 */
int btree_cleaning_clean(struct cleaner *);

/**
 * btree_cleaning_stop(C):
 * Stop the background cleaning for which the cookie ${C} was returned by
 * clean_start.
 */
void btree_cleaning_stop(struct cleaner *);

#endif /* !_BTREE_CLEANING_H_ */

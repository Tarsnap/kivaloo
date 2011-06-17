#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"

/**
 * node_alloc(pagenum, oldestleaf, pagesize):
 * Create and return a node with the specified ${pagenum}, ${oldestleaf}, and
 * ${pagesize} of type NODE_TYPE_NP.
 */
struct node *
node_alloc(uint64_t pagenum, uint64_t oldestleaf, uint32_t pagesize)
{
	struct node * N;

	/* Allocate node. */
	if ((N = malloc(sizeof(struct node))) == NULL)
		goto err0;
	memset(N, 0, sizeof(struct node));

	/* Initialize values. */
	N->pagenum = pagenum;
	N->oldestleaf = oldestleaf;
	N->oldestncleaf = oldestleaf;
	N->pagesize = pagesize;
	N->type = NODE_TYPE_NP;
	N->state = NODE_STATE_CLEAN;
	N->needmerge = 1;
	N->height = -1;
	N->nkeys = (size_t)(-1);
	N->pagebuf = NULL;

	/* Success! */
	return (N);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * node_free(N):
 * Free the node ${N}, which must have type NODE_TYPE_NP.
 */
void
node_free(struct node * N)
{

	/* Be compatible with free(NULL). */
	if (N == NULL)
		return;

	/* Free node. */
	free(N);
}

#include <sys/time.h>

#include <stdlib.h>
#include <string.h>

#include "elasticarray.h"
#include "monoclock.h"
#include "sock.h"

#include "s3_serverpool.h"

/* S3 endpoint structure. */
struct s3_endpoint {
	struct sock_addr * sa;
	struct timeval eol;
};

/* Opaque S3 server pool structure type, is really... */
struct s3_serverpool;

/* ... an elastic array of endpoints. */
ELASTICARRAY_DECL(S3_SERVERPOOL, serverpool, struct s3_endpoint);

/* Free the specified endpoint. */
static void
endpoint_free(S3_SERVERPOOL S, size_t i)
{

	/* Free the endpoint address. */
	sock_addr_free(serverpool_get(S, i)->sa);

	/*
	 * Copy the last endpoint over this one (if we're deleting the last
	 * endpoint in the array, this turns out to be a no-op, of course).
	 */
	memcpy(serverpool_get(S, i),
	    serverpool_get(S, serverpool_getsize(S) - 1),
	    sizeof(struct s3_endpoint));

	/* Delete the last endpoint (now that it's moved). */
	serverpool_shrink(S, 1);
}

/**
 * s3_serverpool_init(void):
 * Create a pool of S3 servers.
 */
struct s3_serverpool *
s3_serverpool_init(void)
{
	S3_SERVERPOOL S;

	S = serverpool_init(0);
	return ((struct s3_serverpool *)S);
}

/**
 * s3_serverpool_add(SP, sa, ttl):
 * Add the address ${sa} to the server pool ${SP} for the next ${ttl} seconds.
 * (If already in the pool, update the expiry time.)
 */
int
s3_serverpool_add(struct s3_serverpool * SP,
    const struct sock_addr *sa, int ttl)
{
	S3_SERVERPOOL S = (S3_SERVERPOOL)SP;
	struct timeval tv;
	struct timeval * tvo;
	struct s3_endpoint ep;
	size_t i;

	/* Compute EOL for this endpoint. */
	if (monoclock_get(&tv))
		goto err0;
	tv.tv_sec += ttl;

	/* Look for the endpoint. */
	for (i = 0; i < serverpool_getsize(S); i++) {
		if (sock_addr_cmp(sa, serverpool_get(S, i)->sa) == 0) {
			/* Update the EOL and return. */
			tvo = &serverpool_get(S, i)->eol;
			if ((tvo->tv_sec < tv.tv_sec) ||
			    ((tvo->tv_sec == tv.tv_sec) &&
			     (tvo->tv_usec < tv.tv_usec))) {
				tvo->tv_sec = tv.tv_sec;
				tvo->tv_usec = tv.tv_usec;
			}
			return (0);
		}
	}

	/* Construct a new endpoint structure. */
	if ((ep.sa = sock_addr_dup(sa)) == NULL)
		goto err0;
	ep.eol.tv_sec = tv.tv_sec;
	ep.eol.tv_usec = tv.tv_usec;

	/* Add the new endpoint to the list. */
	if (serverpool_append(S, &ep, 1))
		goto err1;

	/* Success! */
	return (0);

err1:
	free(ep.sa);
err0:
	abort();
	/* Failure! */
	return (-1);
}

/**
 * s3_serverpool_pick(SP):
 * Pick an address from ${SP} and return it.  The caller is responsible for
 * freeing the address.
 */
struct sock_addr *
s3_serverpool_pick(struct s3_serverpool * SP)
{
	S3_SERVERPOOL S = (S3_SERVERPOOL)SP;
	struct timeval tv;
	struct timeval * tvo;
	struct sock_addr * sa;
	size_t i;

	/* If we have no endpoints, fail. */
	if (serverpool_getsize(S) == 0)
		goto err0;

	/* Delete expired endpoints; count down to avoid missing anything. */
	if (monoclock_get(&tv))
		goto err0;
	for (i = serverpool_getsize(S); i > 0; i--) {
		/* Is the EOL still in the future? */
		tvo = &serverpool_get(S, i - 1)->eol;
		if ((tvo->tv_sec > tv.tv_sec) ||
		    ((tvo->tv_sec == tv.tv_sec) &&
		     (tvo->tv_usec > tv.tv_usec)))
			continue;

		/* Is this the last remaining endpoint? */
		if (serverpool_getsize(S) == 1)
			continue;

		/* Delete this endpoint. */
		endpoint_free(S, i - 1);
	}

	/* Pick a (non-cryptographically) random endpoint. */
	i = rand() % serverpool_getsize(S);

	/* Make a copy of the address. */
	if ((sa = sock_addr_dup(serverpool_get(S, i)->sa)) == NULL)
		goto err0;

	/* Return the address. */
	return (sa);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * s3_serverpool_free(SP):
 * Free the server pool ${SP}.
 */
void
s3_serverpool_free(struct s3_serverpool * SP)
{
	S3_SERVERPOOL S = (S3_SERVERPOOL)SP;

	/* Delete endpoints. */
	while (serverpool_getsize(S) > 0)
		endpoint_free(S, 0);

	/* Free the elastic array. */
	serverpool_free(S);
}

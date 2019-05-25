#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "elasticarray.h"
#include "monoclock.h"
#include "network.h"
#include "noeintr.h"
#include "sock.h"
#include "sock_util.h"
#include "warnp.h"

#include "serverpool.h"

/* Server address. */
struct serverpool_addr {
	struct sock_addr * sa;
	struct timeval eol;
	uint64_t generation;
};

/* Elastic array of server addresses. */
ELASTICARRAY_DECL(SERVERPOOL_ADDRS, serverpool_addrs, struct serverpool_addr);

struct serverpool {
	SERVERPOOL_ADDRS A;
	int s;
	pid_t pid;
	size_t addrlen;
	uint8_t * addr;
	void * read_cookie;
	time_t ttl;
	uint64_t generation;
};

static int callback_read_addr(void *, ssize_t);

/* Add a new address to the pool. */
static int
addaddr(struct serverpool * P, struct sock_addr * sa)
{
	struct serverpool_addr SPA;
	struct serverpool_addr * SPAp;
	size_t i;

	/* Do we already have this address? */
	for (i = 0; i < serverpool_addrs_getsize(P->A); i++) {
		SPAp = serverpool_addrs_get(P->A, i);
		if (sock_addr_cmp(sa, SPAp->sa) == 0) {
			/* Update EOL and generation number. */
			if (monoclock_get(&SPAp->eol))
				goto err0;
			SPAp->eol.tv_sec += P->ttl;
			SPAp->generation = P->generation;

			/* All done. */
			goto done;
		}
	}

	/* Copy address. */
	if ((SPA.sa = sock_addr_dup(sa)) == NULL)
		goto err0;

	/* Record EOL. */
	if (monoclock_get(&SPA.eol))
		goto err1;
	SPA.eol.tv_sec += P->ttl;

	/* Record generation. */
	SPA.generation = P->generation;

	/* Add this structure to the array. */
	if (serverpool_addrs_append(P->A, &SPA, 1))
		goto err1;

done:
	/* Success! */
	return (0);

err1:
	sock_addr_free(SPA.sa);
err0:
	/* Failure! */
	return (-1);
}

/* Remove expired addresses from the pool. */
static int
pruneaddrs(struct serverpool * P)
{
	struct timeval tv;
	struct serverpool_addr * SPA;
	size_t i;

	/* We need to compare address EOLs against the current time. */
	if (monoclock_get(&tv))
		goto err0;

	/* Iterate through the pool. */
	for (i = 0; i < serverpool_addrs_getsize(P->A); i++) {
		SPA = serverpool_addrs_get(P->A, i);

		/*
		 * Keep anything from the current or immediately previous
		 * generation; we need both in case we're running just as
		 * we start reading addresses for a new generation.
		 */
		if ((SPA->generation == P->generation) ||
		    (SPA->generation == P->generation - 1))
			continue;

		/* Keep if the EOL hasn't expired. */
		if ((SPA->eol.tv_sec > tv.tv_sec) ||
		    ((SPA->eol.tv_sec == tv.tv_sec) &&
		     (SPA->eol.tv_usec > tv.tv_usec)))
			continue;

		/* Free this address. */
		sock_addr_free(SPA->sa);

		/* Replace this with the last structure. */
		memcpy(SPA, serverpool_addrs_get(P->A,
		    serverpool_addrs_getsize(P->A) - 1),
		    sizeof(struct serverpool_addr));

		/* Shrink the array (now that the last element has moved). */
		serverpool_addrs_shrink(P->A, 1);
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Callback: We have an address length. */
static int
callback_read_len(void * cookie, ssize_t readlen)
{
	struct serverpool * P = cookie;

	/* We should get -1 (error), 0 (EOF), or a size_t. */
	assert((readlen == -1) || (readlen == 0) ||
	    (readlen == sizeof(size_t)));

	/* Handle failures. */
	if (readlen == -1)
		goto err1;
	if (readlen == 0)
		goto eof;

	/* Sanity-check. */
	if (P->addrlen > SSIZE_MAX)
		goto err1;

	/* An address length of 0 indicates the end of a generation. */
	if (P->addrlen == 0) {
		P->generation++;

		/* Remove expired addresses from the pool. */
		if (pruneaddrs(P))
			goto err0;

		/* Read another address length. */
		if ((P->read_cookie = network_read(P->s,
		    (uint8_t *)&P->addrlen, sizeof(size_t), sizeof(size_t),
		    callback_read_len, P)) == NULL)
			goto err1;
	} else {
		/* Allocate space to hold the address. */
		if ((P->addr = malloc(P->addrlen)) == NULL)
			goto err0;

		/* Read the address. */
		if ((P->read_cookie = network_read(P->s, P->addr, P->addrlen,
		    P->addrlen, callback_read_addr, P)) == NULL)
			goto err1;
	}

	/* Success! */
	return (0);

eof:
	warn0("DNS lookup process died");

	/* Failure! */
	return (-1);

err1:
	warnp("Error reading address via socket");
err0:
	/* Failure! */
	return (-1);
}

/* Callback: We have a (serialized) address. */
static int
callback_read_addr(void * cookie, ssize_t readlen)
{
	struct serverpool * P = cookie;
	struct sock_addr * sa = NULL;

	/* We should get -1 (error), 0 (EOF), or a complete address. */
	assert((readlen == -1) || (readlen == 0) || (readlen == P->addrlen));

	/* Handle failures. */
	if (readlen == -1)
		goto err2;
	if (readlen == 0)
		goto eof;

	/* Parse the address. */
	if ((sa = sock_addr_deserialize(P->addr, P->addrlen)) == NULL)
		goto err0;

	/* Free the serialized address. */
	free(P->addr);
	P->addr = NULL;

	/* Add the address to the pool. */
	if (addaddr(P, sa))
		goto err1;

	/* Start reading another address. */
	if ((P->read_cookie = network_read(P->s, (uint8_t *)&P->addrlen,
	    sizeof(size_t), sizeof(size_t), callback_read_len, P)) == NULL)
		goto err2;

	/* Free the parsed address. */
	sock_addr_free(sa);

	/* Success! */
	return (0);

eof:
	warn0("DNS lookup process died");

	/* Failure! */
	return (-1);

err2:
	warnp("Error reading address via socket");
err1:
	sock_addr_free(sa);
err0:
	/* Failure! */
	return (-1);
}

/* Fork off a child process to perform DNS lookups. */
static pid_t
forkdns(const char * target, int readfd, int writefd, unsigned int freq)
{
	pid_t pid;
	struct sock_addr ** sas;
	uint8_t * addr;
	size_t addrlen;
	size_t i;

	/* Fork a child to perform the DNS lookups. */
	if ((pid = fork()) != 0)
		return (pid);

	/*
	 * In child process.  Close read socket so that we will notice if we
	 * try to write after the parent dies.
	 */
	close(readfd);

	/* Become a session leader. */
	setsid();

	/*
	 * Infinite loop doing DNS lookups and writing them to our parent via
	 * the socket, sleeping ${freq} seconds between each lookup.
	 */
	for (;; sleep(freq)) {
		/*
		 * Perform a DNS lookup.  Keep going if it fails; we don't
		 * want to die as a result of a temporary network glitch.
		 */
		if ((sas = sock_resolve(target)) == NULL)
			continue;

		/* Send each address over. */
		for (i = 0; sas[i] != NULL; i++) {
			/* Serialize the address. */
			if (sock_addr_serialize(sas[i], &addr, &addrlen))
				goto die;

			/* Send it to our parent. */
			if (noeintr_write(writefd, &addrlen,
			    sizeof(addrlen)) == -1)
				goto die;
			if (noeintr_write(writefd, addr, addrlen) == -1)
				goto die;

			/* Free the serialized address. */
			free(addr);
		}

		/* Free the list of addresses. */
		sock_addr_freelist(sas);

		/* Send a zero over to denote the end of a generation. */
		addrlen = 0;
		if (noeintr_write(writefd, &addrlen, sizeof(addrlen)) == -1)
			goto die;
	}

	/* NOTREACHED */

die:
	_exit(1);
}

/**
 * serverpool_create(target, freq, ttl):
 * Fork off a process to perform DNS lookups for ${target}, sleeping ${freq}
 * seconds between lookups.  Keep returned addresses for ${ttl} seconds, and
 * always keep at least one address (even if no addresses have been returned
 * in the past ${ttl} seconds).  Return a cookie which can be passed to
 * serverpool_pick().
 */
struct serverpool *
serverpool_create(const char * target, unsigned int freq, time_t ttl)
{
	struct serverpool * P;
	struct sock_addr ** sas;
	int fd[2];
	size_t i;

	/* Allocate server pool structure and (empty) array. */
	if ((P = malloc(sizeof(struct serverpool))) == NULL)
		goto err0;
	if ((P->A = serverpool_addrs_init(0)) == NULL)
		goto err1;

	/* Record TTL for future reference. */
	P->ttl = ttl;

	/* The first addresses will be generation #0. */
	P->generation = 0;

	/* Perform a first lookup. */
	if ((sas = sock_resolve(target)) == NULL)
		goto err2;

	/* Stuff the results into the server pool address list. */
	for (i = 0; sas[i] != NULL; i++) {
		/* Add this address to the pool. */
		if (addaddr(P, sas[i]))
			goto err3;
	}

	/* Next addresses will be generation #1. */
	P->generation = 1;

	/* Create a socket for the child to send us addresses. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
		warnp("socketpair");
		goto err3;
	}

	/* Mark the read end of the socket pair as non-blocking. */
	if (fcntl(fd[0], F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make dns socket non-blocking");
		goto err4;
	}

	/* Fork off the process to perform DNS lookups. */
	if ((P->pid = forkdns(target, fd[0], fd[1], freq)) == -1)
		goto err4;

	/* Start reading from the child. */
	P->s = fd[0];
	P->addr = NULL;
	if ((P->read_cookie = network_read(P->s, (uint8_t *)&P->addrlen,
	    sizeof(size_t), sizeof(size_t), callback_read_len, P)) == NULL) {
		warnp("Error reading address via socket");
		goto err5;
	}

	/* Close write socket; only the child needs it. */
	close(fd[1]);

	/* Free the initial list of addresses. */
	sock_addr_freelist(sas);

	/* Success! */
	return (P);

err5:
	kill(P->pid, SIGTERM);
err4:
	close(fd[1]);
	close(fd[0]);
err3:
	sock_addr_freelist(sas);
	for (i = serverpool_addrs_getsize(P->A); i > 0; i--)
		sock_addr_free(serverpool_addrs_get(P->A, i - 1)->sa);
err2:
	serverpool_addrs_free(P->A);
err1:
	free(P);
err0:
	return (NULL);
}

/**
 * serverpool_pick(P):
 * Return an address randomly selected from the addresses in the pool ${P}.
 * The callers is responsible for freeing the address.
 */
struct sock_addr *
serverpool_pick(struct serverpool * P)
{
	uint64_t i;

	/* Get rid of any expired addresses. */
	if (pruneaddrs(P))
		goto err0;

	/* We should always have at least one address at this point. */
	assert(serverpool_addrs_getsize(P->A) > 0);

	/* Pick a (non-cryptographically) random address. */
	i = (size_t)rand() % serverpool_addrs_getsize(P->A);

	/* Return a copy of the address. */
	return (sock_addr_dup(serverpool_addrs_get(P->A, i)->sa));

err0:
	/* Failure! */
	return (NULL);
}

/**
 * serverpool_free(P):
 * Stop performing DNS lookups and free the server pool ${P}.
 */
void
serverpool_free(struct serverpool * P)
{
	size_t i;

	/* Behave consistently with free(NULL). */
	if (P == NULL)
		return;

	/* Signal the child process to die. */
	kill(P->pid, SIGTERM);

	/* Stop reading addresses. */
	network_read_cancel(P->read_cookie);

	/* Free the address buffer, if we have a read in progress. */
	free(P->addr);

	/* Free addresses. */
	for (i = serverpool_addrs_getsize(P->A); i > 0; i--)
		sock_addr_free(serverpool_addrs_get(P->A, i - 1)->sa);

	/* Free the list of addresses. */
	serverpool_addrs_free(P->A);

	/* Free the server pool. */
	free(P);
}

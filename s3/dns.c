#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "network.h"
#include "noeintr.h"
#include "s3_request_queue.h"
#include "sock.h"
#include "sock_util.h"
#include "warnp.h"

#include "dns.h"

/* Opaque types. */
struct dns_reader {
	struct s3_request_queue * Q;
	int s;
	pid_t pid;
	size_t addrlen;
	uint8_t * addr;
	void * read_cookie;
};

static int callback_read_len(void *, ssize_t);
static int callback_read_addr(void *, ssize_t);

/* Child process: Perform DNS lookups and write results via socket. */
static void
dnsrun(const char * target, int s)
{
	struct sock_addr ** sas;
	uint8_t * addr;
	size_t addrlen;
	size_t i;

	/*
	 * Infinite loop doing DNS lookups and writing them to our parent via
	 * the socket, sleeping 10 seconds between each lookup.
	 */
	for (;; sleep(10)) {
		/* Perform a DNS lookup. */
		if ((sas = sock_resolve(target)) == NULL)
			continue;

		/* Send each address over. */
		for (i = 0; sas[i] != NULL; i++) {
			/* Serialize the address. */
			if (sock_addr_serialize(sas[i], &addr, &addrlen))
				goto die;

			/* Send it to our parent. */
			if (noeintr_write(s, &addrlen, sizeof(addrlen)) == -1)
				goto die;
			if (noeintr_write(s, addr, addrlen) == -1)
				goto die;

			/* Free the serialized address. */
			free(addr);
		}

		/* Free the list of addresses. */
		sock_addr_freelist(sas);
	}

	/* NOTREACHED */

die:
	_exit(1);
}

/* Callback: We have an address length. */
static int
callback_read_len(void * cookie, ssize_t readlen)
{
	struct dns_reader * DR = cookie;

	/* If we failed to read a size_t, something is seriously wrong. */
	if (readlen != sizeof(size_t))
		goto err1;

	/* Sanity-check. */
	if (DR->addrlen > SSIZE_MAX)
		goto err1;

	/* Allocate space to hold the address. */
	if ((DR->addr = malloc(DR->addrlen)) == NULL)
		goto err0;

	/* Read the address. */
	if ((DR->read_cookie = network_read(DR->s, DR->addr, DR->addrlen,
	    DR->addrlen, callback_read_addr, DR)) == NULL)
		goto err1;

	/* Success! */
	return (0);

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
	struct dns_reader * DR = cookie;
	struct sock_addr * sa;

	/* If we failed to read, something is seriously wrong. */
	if (readlen != (ssize_t)DR->addrlen)
		goto err1;

	/* Parse the address. */
	if ((sa = sock_addr_deserialize(DR->addr, DR->addrlen)) == NULL)
		goto err0;

	/* Free the serialized address. */
	free(DR->addr);
	DR->addr = NULL;

	/* Add the address to the S3 request queue. */
	if (s3_request_queue_addaddr(DR->Q, sa, 600)) {
		warnp("Error adding S3 endpoint address");
		sock_addr_free(sa);
		goto err0;
	}

	/* Free the parsed address. */
	sock_addr_free(sa);

	/* Start reading another address. */
	if ((DR->read_cookie = network_read(DR->s, (uint8_t *)&DR->addrlen,
	    sizeof(size_t), sizeof(size_t), callback_read_len, DR)) == NULL)
		goto err1;

	/* Success! */
	return (0);

err1:
	warnp("Error reading address via socket");
err0:
	/* Failure! */
	return (-1);
}

/**
 * dns_reader_start(Q, target):
 * Start performing DNS lookups for ${target}, feeding resulting addresses
 * into ${Q}.  Return a cookie which can be passed to dns_reader_stop.
 */
struct dns_reader *
dns_reader_start(struct s3_request_queue * Q, const char * target)
{
	struct dns_reader * DR;
	int fd[2];
	pid_t pid;

	/* Create a socket for feeding addresses back. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) {
		warnp("socketpair");
		goto err0;
	}

	/* Mark the read end of the socket pair as non-blocking. */
	if (fcntl(fd[0], F_SETFL, O_NONBLOCK) == -1) {
		warnp("Cannot make dns socket non-blocking");
		goto err1;
	}

	/* Fork a child to perform the DNS lookups. */
	switch ((pid = fork())) {
	case -1:
		/* Fork failed. */
		warnp("fork");
		goto err1;
	case 0:
		/*
		 * In child process.  Close read socket so that if the
		 * parent dies or is killed we will notice the socket being
		 * reset and will stop doing DNS lookups.
		 */
		while (close(fd[0])) {
			if (errno == EINTR)
				continue;
			_exit(1);
		}

		/* Perform DNS lookups and write the results to the socket. */
		dnsrun(target, fd[1]);
		/* NOTREACHED */
	default:
		/* In parent process. */
		break;
	}

	/* Bake a cookie. */
	if ((DR = malloc(sizeof(struct dns_reader))) == NULL)
		goto err2;
	DR->Q = Q;
	DR->s = fd[0];
	DR->pid = pid;
	DR->addr = NULL;
	DR->read_cookie = NULL;

	/* Start reading an address. */
	if ((DR->read_cookie = network_read(DR->s, (uint8_t *)&DR->addrlen,
	    sizeof(size_t), sizeof(size_t), callback_read_len, DR)) == NULL) {
		warnp("Error reading address via socket");
		goto err3;
	}

	/* Close write socket so that we'll get EOF if the child dies. */
	while (close(fd[1])) {
		if (errno == EINTR)
			continue;
		warnp("close");
		goto err3;	/* We'll close again, but it's harmless. */
	}

	/* Success! */
	return (DR);

err3:
	free(DR);
err2:
	kill(SIGTERM, pid);
err1:
	close(fd[1]);
	close(fd[0]);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * dns_reader_stop(DR):
 * Stop the DNS reader ${DR}.
 */
void
dns_reader_stop(struct dns_reader * DR)
{

	/* Signal the child process to die. */
	kill(SIGTERM, DR->pid);

	/* Stop reading addresses. */
	network_read_cancel(DR->read_cookie);

	/* Free the address buffer, if we have one. */
	free(DR->addr);

	/* Free the cookie. */
	free(DR);
}

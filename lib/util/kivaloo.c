#include <stdlib.h>
#include <unistd.h>

#include "sock.h"
#include "warnp.h"
#include "wire.h"

#include "kivaloo.h"

struct kivaloo_cookie {
	struct wire_requestqueue * Q;
	int s;
	struct sock_addr ** sas;
};

/**
 * kivaloo_open(addr, Q):
 * Resolve the socket address ${addr}, connect to it, and create a wire
 * request queue.  Return the request queue via ${Q}; and return a cookie
 * which can be passed to kivaloo_close() to shut down the queue and release
 * resources.
 */
void *
kivaloo_open(const char * addr, struct wire_requestqueue ** Q)
{
	struct kivaloo_cookie * K;

	/* Allocate a cookie. */
	if ((K = malloc(sizeof(struct kivaloo_cookie))) == NULL)
		goto err0;

	/* Resolve the target address. */
	if ((K->sas = sock_resolve(addr)) == NULL) {
		warnp("Error resolving socket address: %s", addr);
		goto err1;
	}
	if (K->sas[0] == NULL) {
		warn0("No addresses found for %s", addr);
		goto err2;
	}

	/* Open a connection to it. */
	if ((K->s = sock_connect(K->sas)) == -1)
		goto err2;

	/* Create a request queue. */
	if ((K->Q = wire_requestqueue_init(K->s)) == NULL) {
		warnp("Cannot create request queue");
		goto err3;
	}

	/* Return the request queue and cookie. */
	*Q = K->Q;
	return (K);

err3:
	if (close(K->s))
		warnp("close");
err2:
	sock_addr_freelist(K->sas);
err1:
	free(K);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * kivaloo_close(cookie):
 * Destroy and free the wire request queue, close the socket and free memory
 * allocated by the kivaloo_open() which returned ${cookie}.
 */
void
kivaloo_close(void * cookie)
{
	struct kivaloo_cookie * K = cookie;

	/* Shut down the request queue. */
	wire_requestqueue_destroy(K->Q);
	wire_requestqueue_free(K->Q);

	/* Close the socket. */
	if (close(K->s))
		warnp("close");

	/* Free socket addresses. */
	sock_addr_freelist(K->sas);

	/* Free our cookie. */
	free(K);
}

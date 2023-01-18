/**
 * APISUPPORT CFLAGS: LIBSSL_HOST_NAME
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "events.h"
#include "warnp.h"

#include "network_ssl.h"
#include "network_ssl_compat.h"

/* Compatibility for OpenSSL versions. */
#ifdef NETWORK_SSL_COMPAT_TLS_CLIENT_METHOD
#define TLS_client_method network_ssl_compat_TLS_client_method
#endif

#ifdef NETWORK_SSL_COMPAT_SET_MIN_PROTO_VERSION
#define SSL_CTX_set_min_proto_version network_ssl_compat_CTL_set_min_proto_version
#endif

#ifdef NETWORK_SSL_COMPAT_CHECK_HOSTNAME
#define SSL_set1_host network_ssl_compat_set1_host
#define SSL_set_hostflags network_ssl_compat_set_hostflags
#endif

#ifdef NETWORK_SSL_COMPAT_READ_WRITE_EX
#define SSL_read_ex network_ssl_compat_read_ex
#define SSL_write_ex network_ssl_compat_write_ex
#endif

/* SSL context in which to create connections. */
static SSL_CTX * ctx = NULL;

/* Internal state. */
struct network_ssl_ctx {
	/* SSL management state. */
	int s;
	SSL * ssl;
	int waiting_r;
	int waiting_w;
	void * immediate_cookie;

	/* Pending read operation. */
	int (*read_callback)(void *, ssize_t);
	void * read_cookie;
	uint8_t * read_buf;
	size_t read_buflen;
	size_t read_minlen;
	size_t read_bufpos;
	int read_needs_r;
	int read_needs_w;

	/* Pending write operation. */
	int (*write_callback)(void *, ssize_t);
	void * write_cookie;
	const uint8_t * write_buf;
	size_t write_buflen;
	size_t write_minlen;
	size_t write_bufpos;
	int write_needs_r;
	int write_needs_w;
};

/* Try to SSL_read/SSL_write if possible. */
static int poke(struct network_ssl_ctx *);

/* Free the SSL context upon exit. */
static void
network_ssl_atexit(void)
{

	assert(ctx != NULL);
	SSL_CTX_free(ctx);
	ctx = NULL;
}

/* Initialize SSL library. */
static int
init(void)
{
	const SSL_METHOD * meth;

	/* If we have a context already, there's nothing to do. */
	if (ctx != NULL)
		return (0);

	/* Launch SSL. */
	if (!SSL_library_init()) {
		warn0("Could not initialize SSL library");
		goto err0;
	}

	/* We want to use TLS. */
	if ((meth = TLS_client_method()) == NULL) {
		warn0("TLS_client_method");
		goto err0;
	}

	/* Create an SSL context. */
	if ((ctx = SSL_CTX_new(meth)) == NULL) {
		warn0("Could not create SSL context");
		goto err0;
	}

	/* Insist on a minimum of TLS 1.2. */
	if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
		warn0("Could not set minimum TLS version");
		goto err1;
	}

	/* Partial writes are a thing. */
	SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

	/* Load root certificates. */
	if (!SSL_CTX_set_default_verify_paths(ctx)) {
		warn0("Could not load default root certificates");
		goto err1;
	}

	/* Release the context when we exit. */
	atexit(network_ssl_atexit);

	/* Success! */
	return (0);

err1:
	SSL_CTX_free(ctx);
	ctx = NULL;
err0:
	/* Failure! */
	return (-1);
}

/* Socket ready for reading. */
static int
callback_read(void * cookie)
{
	struct network_ssl_ctx * ssl = cookie;

	/* No longer waiting for socket to be readable. */
	ssl->waiting_r = 0;

	/*
	 * Any operations which were waiting for the socket to become
	 * readable are now free to run.
	 */
	ssl->read_needs_r = 0;
	ssl->write_needs_r = 0;

	/* Poke the SSL stack. */
	return (poke(ssl));
}

/* Socket ready for writing. */
static int
callback_write(void * cookie)
{
	struct network_ssl_ctx * ssl = cookie;

	/* No longer waiting for socket to be writable. */
	ssl->waiting_w = 0;

	/*
	 * Any operations which were waiting for the socket to become
	 * writable are now free to run.
	 */
	ssl->read_needs_w = 0;
	ssl->write_needs_w = 0;

	/* Poke the SSL stack. */
	return (poke(ssl));
}

/* Immediate callback from register functions. */
static int
callback_immediate(void * cookie)
{
	struct network_ssl_ctx * ssl = cookie;

	/* No longer waiting for an immediate callback. */
	ssl->immediate_cookie = NULL;

	/* Poke the SSL stack. */
	return (poke(ssl));
}

/* Register or cancel network events as needed. */
static int
setupevents(struct network_ssl_ctx * ssl)
{

	/* Cancel network events if no longer needed. */
	if (ssl->waiting_r && !ssl->read_needs_r && !ssl->write_needs_r) {
		if (events_network_cancel(ssl->s, EVENTS_NETWORK_OP_READ))
			goto err0;
		ssl->waiting_r = 0;
	}
	if (ssl->waiting_w && !ssl->read_needs_w && !ssl->write_needs_w) {
		if (events_network_cancel(ssl->s, EVENTS_NETWORK_OP_WRITE))
			goto err0;
		ssl->waiting_w = 0;
	}

	/* Register network events if needed. */
	if (!ssl->waiting_r && (ssl->read_needs_r || ssl->write_needs_r)) {
		if (events_network_register(callback_read, ssl, ssl->s,
		    EVENTS_NETWORK_OP_READ))
			goto err0;
		ssl->waiting_r = 1;
	}
	if (!ssl->waiting_w && (ssl->read_needs_w || ssl->write_needs_w)) {
		if (events_network_register(callback_write, ssl, ssl->s,
		    EVENTS_NETWORK_OP_WRITE))
			goto err0;
		ssl->waiting_w = 1;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Invoke a callback. */
static int
docallback(int (*callback)(void *, ssize_t), void * cookie, ssize_t len,
    int (**callback_ptr)(void *, ssize_t))
{

	/*
	 * Zero the callback pointer; the callback might want to request
	 * another read/write operation so we need to get out of the way.
	 */
	*callback_ptr = NULL;

	/* Perform the callback. */
	return ((callback)(cookie, len));
}

/* Try to SSL_read. */
static int
doread(struct network_ssl_ctx * ssl)
{
	size_t len;
	int sslerr;
	int ret;
#ifndef SO_NOSIGPIPE
	void (*oldsig)(int);
#endif

	/*
	 * We need to zero errno in order to distinguish socket EOF from
	 * a socket error.
	 */
	errno = 0;

	/*
	 * Flush any errors internal to the SSL stack; otherwise if we
	 * encounter an error we won't be able to identify which error
	 * needs to be reported.
	 */
	ERR_clear_error();

	/* If we don't have SO_NOSIGPIPE, ignore SIGPIPE. */
#ifndef SO_NOSIGPIPE
	if ((oldsig = signal(SIGPIPE, SIG_IGN)) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		return (-1);
	}
#endif

	/* Ask the SSL stack to read some data. */
	while ((ret = SSL_read_ex(ssl->ssl, &ssl->read_buf[ssl->read_bufpos],
	    ssl->read_buflen - ssl->read_bufpos, &len)) > 0) {
		/* Got some data. */
		ssl->read_bufpos += len;

		/* Sanity-check. */
		assert(ssl->read_bufpos <= ssl->read_buflen);

		/* Do we have enough? */
		if (ssl->read_bufpos >= ssl->read_minlen) {
			/* If we ignored SIGPIPE, restore the old handler. */
#ifndef SO_NOSIGPIPE
			if (signal(SIGPIPE, oldsig) == SIG_ERR) {
				warnp("signal(SIGPIPE)");
				return (-1);
			}
#endif

			return (docallback(ssl->read_callback,
			    ssl->read_cookie, (ssize_t)ssl->read_bufpos,
			    &ssl->read_callback));
		}
	}

	/* If we ignored SIGPIPE, restore the old handler. */
#ifndef SO_NOSIGPIPE
	if (signal(SIGPIPE, oldsig) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		return (-1);
	}
#endif

	/* SSL_read_ex couldn't give us any data... why? */
	switch ((sslerr = SSL_get_error(ssl->ssl, ret))) {
	case SSL_ERROR_WANT_READ:
		/* Nothing to do until the socket is readable. */
		ssl->read_needs_r = 1;
		return (0);
	case SSL_ERROR_WANT_WRITE:
		/* Nothing to do until the socket is writable. */
		ssl->read_needs_w = 1;
		return (0);
	case SSL_ERROR_SYSCALL:
		/* Either a socket EOF or a legitimate error. */
		if (errno != 0)
			break;

		/* FALLTHROUGH */
	case SSL_ERROR_ZERO_RETURN:
		/* Connection EOF. */
		return (docallback(ssl->read_callback,
		    ssl->read_cookie, 0, &ssl->read_callback));
	case SSL_ERROR_SSL:
		/* SSL failure, probably a protocol error. */
		warn0("SSL failure: %s",
		    ERR_error_string(ERR_get_error(), NULL));
		break;
	default:
		/* Don't know what happened here.  Fail the read. */
		warn0("Unknown SSL error: %d", sslerr);
		break;
	}

	/* Connection failure. */
	return (docallback(ssl->read_callback, ssl->read_cookie, -1,
	    &ssl->read_callback));
}

/* Try to SSL_write. */
static int
dowrite(struct network_ssl_ctx * ssl)
{
	size_t len;
	int sslerr;
	int ret;
#ifndef SO_NOSIGPIPE
	void (*oldsig)(int);
#endif

	/*
	 * Flush any errors internal to the SSL stack; otherwise if we
	 * encounter an error we won't be able to identify which error
	 * needs to be reported.
	 */
	ERR_clear_error();

	/* If we don't have SO_NOSIGPIPE, ignore SIGPIPE. */
#ifndef SO_NOSIGPIPE
	if ((oldsig = signal(SIGPIPE, SIG_IGN)) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		return (-1);
	}
#endif

	/* Ask the SSL stack to write some data. */
	while ((ret = SSL_write_ex(ssl->ssl, &ssl->write_buf[ssl->write_bufpos],
	    ssl->write_buflen - ssl->write_bufpos, &len)) > 0) {
		/* We wrote some data. */
		ssl->write_bufpos += len;

		/* Sanity-check. */
		assert(ssl->write_bufpos <= ssl->write_buflen);

		/* Have we written enough? */
		if (ssl->write_bufpos >= ssl->write_minlen) {
			/* If we ignored SIGPIPE, restore the old handler. */
#ifndef SO_NOSIGPIPE
			if (signal(SIGPIPE, oldsig) == SIG_ERR) {
				warnp("signal(SIGPIPE)");
				return (-1);
			}
#endif

			return (docallback(ssl->write_callback,
			    ssl->write_cookie, (ssize_t)ssl->write_bufpos,
			    &ssl->write_callback));
		}
	}

	/* If we ignored SIGPIPE, restore the old handler. */
#ifndef SO_NOSIGPIPE
	if (signal(SIGPIPE, oldsig) == SIG_ERR) {
		warnp("signal(SIGPIPE)");
		return (-1);
	}
#endif

	/* SSL_write_ex couldn't send any data... why? */
	switch ((sslerr = SSL_get_error(ssl->ssl, ret))) {
	case SSL_ERROR_WANT_READ:
		/* Nothing to do until the socket is readable. */
		ssl->write_needs_r = 1;
		return (0);
	case SSL_ERROR_WANT_WRITE:
		/* Nothing to do until the socket is writable. */
		ssl->write_needs_w = 1;
		return (0);
	case SSL_ERROR_SYSCALL:
		/* Something bad happened to our connection. */
		break;
	case SSL_ERROR_SSL:
		/* SSL failure, probably a protocol error. */
		warn0("SSL failure: %s",
		    ERR_error_string(ERR_get_error(), NULL));
		break;
	default:
		/* Don't know what happened here.  Fail the write. */
		warn0("Unknown SSL error: %d", sslerr);
		break;
	}

	/* Connection failure. */
	return (docallback(ssl->write_callback, ssl->write_cookie, -1,
	    &ssl->write_callback));
}

/* Try to SSL_read/SSL_write if possible. */
static int
poke(struct network_ssl_ctx * ssl)
{

	/* Should we try to read? */
	if (ssl->read_callback != NULL &&
	    !ssl->read_needs_r && !ssl->read_needs_w) {
		if (doread(ssl))
			goto err0;
	}

	/* Should we try to write? */
	if (ssl->write_callback != NULL &&
	    !ssl->write_needs_r && !ssl->write_needs_w) {
		if (dowrite(ssl))
			goto err0;
	}

	/* Wait for socket readability/writability as needed. */
	if (setupevents(ssl))
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * network_ssl_open(s, hostname):
 * Prepare to communicate using TLS over the socket ${s} to a host named
 * ${hostname}.  Return a context which can be passed to network_ssl_read(),
 * network_ssl_write(), and network_ssl_close().
 */
struct network_ssl_ctx *
network_ssl_open(int s, const char * hostname)
{
	struct network_ssl_ctx * ssl;
#ifdef SO_NOSIGPIPE
	int val = 1;
#endif

	/* Make sure we've initialized properly. */
	if (init())
		goto err0;

	/* Allocate a state structure. */
	if ((ssl = malloc(sizeof(struct network_ssl_ctx))) == NULL)
		goto err0;
	ssl->s = s;
	ssl->waiting_r = ssl->waiting_w = 0;
	ssl->immediate_cookie = NULL;
	ssl->read_callback = NULL;
	ssl->read_cookie = NULL;
	ssl->read_buf = NULL;
	ssl->read_buflen = ssl->read_minlen = ssl->read_bufpos = 0;
	ssl->read_needs_r = ssl->read_needs_w = 0;
	ssl->write_callback = NULL;
	ssl->write_cookie = NULL;
	ssl->write_buf = NULL;
	ssl->write_buflen = ssl->write_minlen = ssl->write_bufpos = 0;
	ssl->write_needs_r = ssl->write_needs_w = 0;

	/* If we have SO_NOSIGPIPE, apply it to the socket. */
#ifdef SO_NOSIGPIPE
	if (setsockopt(ssl->s, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
		warnp("setsockopt(SO_NOSIGPIPE)");
		goto err1;
	}
#endif

	/* Create an SSL connection state within the SSL context. */
	if ((ssl->ssl = SSL_new(ctx)) == NULL) {
		warn0("SSL_new");
		goto err1;
	}

	/* Attach the provided socket to the SSL connection. */
	if (!SSL_set_fd(ssl->ssl, ssl->s)) {
		warn0("SSL_set_fd");
		goto err2;
	}

	/* Enable SNI; some servers need this to send us the right cert. */
	if (!SSL_set_tlsext_host_name(ssl->ssl, hostname)) {
		warn0("SSL_set_tlsext_host_name");
		goto err2;
	}

	/* Tell OpenSSL which host we're trying to talk to... */
	if (!SSL_set1_host(ssl->ssl, hostname)) {
		warn0("SSL_set1_host");
		goto err2;
	}

	/* ... and ask it to make sure that this is what is happening. */
	SSL_set_hostflags(ssl->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
	SSL_set_verify(ssl->ssl, SSL_VERIFY_PEER, NULL);

	/* Set ssl to work in client mode. */
	SSL_set_connect_state(ssl->ssl);

	/* Success! */
	return (ssl);

err2:
	SSL_free(ssl->ssl);
err1:
	free(ssl);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_ssl_read(ssl, buf, buflen, minread, callback, cookie):
 * Behave as network_read(), but take a network SSL context instead of a
 * file descriptor.  Return a cookie which can be passed to
 * network_ssl_read_cancel().
 */
void *
network_ssl_read(struct network_ssl_ctx * ssl, uint8_t * buf,
    size_t buflen, size_t minread,
    int (* callback)(void *, ssize_t), void * cookie)
{

	/* We must be initialized before we get here. */
	assert(ctx != NULL);

	/* Make sure buflen is non-zero. */
	assert(buflen != 0);

	/* Sanity-check: # bytes must fit into a ssize_t. */
	assert(buflen <= SSIZE_MAX);

	/* Sanity-check: Must not have a read already in progress. */
	assert(ssl->read_callback == NULL);

	/* Record details of the pending request. */
	ssl->read_callback = callback;
	ssl->read_cookie = cookie;
	ssl->read_buf = buf;
	ssl->read_buflen = buflen;
	ssl->read_minlen = minread;
	ssl->read_bufpos = 0;
	ssl->read_needs_r = 0;
	ssl->read_needs_w = 0;

	/* Poke the SSL stack. */
	if (ssl->immediate_cookie == NULL) {
		if ((ssl->immediate_cookie = events_immediate_register(
		    callback_immediate, ssl, 0)) == NULL)
			goto err0;
	}

	/* Success! */
	return (ssl);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_ssl_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_ssl_read().  Do not invoke the callback associated with the read.
 */
void
network_ssl_read_cancel(void * cookie)
{
	struct network_ssl_ctx * ssl = cookie;

	/* No more pending read request. */
	ssl->read_callback = NULL;

	/*
	 * It's possible that we don't need any more network events; we need
	 * to make sure we deregister our callbacks in order to have them
	 * out of the way of future operations on the underlying socket.
	 */
	ssl->read_needs_r = ssl->read_needs_w = 0;

	/* We're only (potentially) cancelling events now; so this is safe. */
	(void)setupevents(ssl);
}

/**
 * network_ssl_write(ssl, buf, buflen, minwrite, callback, cookie):
 * Behave as network_write(), but take a network SSL context instead of a
 * file descriptor.  Return a cookie which can be passed to
 * network_ssl_write_cancel().
 */
void *
network_ssl_write(struct network_ssl_ctx * ssl, const uint8_t * buf,
    size_t buflen, size_t minwrite,
    int (* callback)(void *, ssize_t), void * cookie)
{

	/* We must be initialized before we get here. */
	assert(ctx != NULL);

	/* Make sure buflen is non-zero. */
	assert(buflen != 0);

	/* Sanity-check: # bytes must fit into a ssize_t. */
	assert(buflen <= SSIZE_MAX);

	/* Sanity-check: Must not have a write already in progress. */
	assert(ssl->write_callback == NULL);

	/* Record details of the pending request. */
	ssl->write_callback = callback;
	ssl->write_cookie = cookie;
	ssl->write_buf = buf;
	ssl->write_buflen = buflen;
	ssl->write_minlen = minwrite;
	ssl->write_bufpos = 0;
	ssl->write_needs_r = 0;
	ssl->write_needs_w = 0;

	/* Poke the SSL stack. */
	if (ssl->immediate_cookie == NULL) {
		if ((ssl->immediate_cookie = events_immediate_register(
		    callback_immediate, ssl, 0)) == NULL)
			goto err0;
	}

	/* Success! */
	return (ssl);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * network_ssl_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_ssl_write().  Do not invoke the callback associated with the write.
 */
void
network_ssl_write_cancel(void * cookie)
{
	struct network_ssl_ctx * ssl = cookie;

	/* No more pending write request. */
	ssl->write_callback = NULL;

	/*
	 * It's possible that we don't need any more network events; we need
	 * to make sure we deregister our callbacks in order to have them
	 * out of the way of future operations on the underlying socket.
	 */
	ssl->write_needs_r = ssl->write_needs_w = 0;

	/* We're only (potentially) cancelling events now; so this is safe. */
	(void)setupevents(ssl);
}

/**
 * network_ssl_close(ssl):
 * Stop performing SSL operations within the provided context.  This cannot
 * be called while there are network_ssl_read() or network_ssl_write()
 * operations pending; and this does not close the underlying socket.
 */
void
network_ssl_close(struct network_ssl_ctx * ssl)
{

	/* Sanity check. */
	assert(ssl != NULL);

	/* Cancel a pending immediate event. */
	if (ssl->immediate_cookie)
		events_immediate_cancel(ssl->immediate_cookie);

	/* Must not have operations in progress. */
	assert(ssl->read_callback == NULL);
	assert(ssl->write_callback == NULL);

	/* It should be impossible for us to have any events registered. */
	assert(ssl->waiting_r == 0);
	assert(ssl->waiting_w == 0);

	/* Shut down and free the SSL context. */
	SSL_shutdown(ssl->ssl);
	SSL_free(ssl->ssl);

	/* Free our state structure. */
	free(ssl);
}

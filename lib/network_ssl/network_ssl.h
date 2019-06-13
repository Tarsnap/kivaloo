#ifndef _NETWORK_SSL_H_
#define _NETWORK_SSL_H_

#include <stdint.h>
#include <unistd.h>

/**
 * network_ssl_open(s, hostname):
 * Prepare to communicate using TLS over the socket ${s} to a host named
 * ${hostname}.  Return a context which can be passed to network_ssl_read,
 * network_ssl_write, and network_ssl_close.
 */
struct network_ssl_ctx * network_ssl_open(int, const char *);

/**
 * network_ssl_read(ssl, buf, buflen, minread, callback, cookie):
 * Behave as network_read, but take a network SSL context instead of a
 * file descriptor.  Return a cookie which can be passed to
 * network_ssl_read_cancel.
 */
void * network_ssl_read(struct network_ssl_ctx *, uint8_t *, size_t, size_t,
    int (*)(void *, ssize_t), void *);

/**
 * network_ssl_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_ssl_read.  Do not invoke the callback associated with the read.
 */
void network_ssl_read_cancel(void *);

/**
 * network_ssl_write(ssl, buf, buflen, minwrite, callback, cookie):
 * Behave as network_write, but take a network SSL context instead of a
 * file descriptor.  Return a cookie which can be passed to
 * network_ssl_write_cancel.
 */
void * network_ssl_write(struct network_ssl_ctx *, const uint8_t *, size_t,
    size_t, int (*)(void *, ssize_t), void *);

/**
 * network_ssl_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_ssl_write.  Do not invoke the callback associated with the write.
 */
void network_ssl_write_cancel(void *);

/**
 * network_ssl_close(ssl):
 * Stop performing SSL operations within the provided context.  This cannot
 * be called while there are network_ssl_read or network_ssl_write operations
 * pending; and this does not close the underlying socket.
 */
void network_ssl_close(struct network_ssl_ctx *);

#endif /* !_NETWORK_SSL_H_ */

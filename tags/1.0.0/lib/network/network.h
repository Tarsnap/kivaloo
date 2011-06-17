#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stddef.h>
#include <stdint.h>

/**
 * network_accept(fd, callback, cookie):
 * Asynchronously accept a connection on the socket ${fd}, which must be
 * already marked as listening and non-blocking.  When a connection has been
 * accepted or an error occurs, invoke ${callback}(${cookie}, s) where s is
 * the accepted connection or -1 on error.  Return a cookie which can be
 * passed to network_accept_cancel in order to cancel the accept.
 */
void * network_accept(int, int (*)(void *, int), void *);

/**
 * network_accept_cancel(cookie);
 * Cancel the connection accept for which the cookie ${cookie} was returned
 * by network_accept.  Do not invoke the callback associated with the accept.
 */
void network_accept_cancel(void *);

/**
 * network_read(fd, buf, buflen, minread, callback, cookie):
 * Asynchronously read up to ${buflen} bytes of data from ${fd} into ${buf}.
 * When at least ${minread} bytes have been read or on error, invoke
 * ${callback}(${cookie}, lenread), where lenread is 0 on error (or EOF) and
 * the number of bytes read (between ${minread} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_read_cancel in
 * order to cancel the read.
 */
void * network_read(int, uint8_t *, size_t, size_t,
    int (*)(void *, size_t), void *);

/**
 * network_read_cancel(cookie):
 * Cancel the buffer read for which the cookie ${cookie} was returned by
 * network_read.  Do not invoke the callback associated with the read.
 */
void network_read_cancel(void *);

/**
 * network_write(fd, buf, buflen, minwrite, callback, cookie):
 * Asynchronously write up to ${buflen} bytes of data from ${buf} to ${fd}.
 * When at least ${minwrite} bytes have been written or on error, invoke
 * ${callback}(${cookie}, lenwrit), where lenwrit is 0 on error (or EOF) and
 * the number of bytes written (between ${minwrite} and ${buflen} inclusive)
 * otherwise.  Return a cookie which can be passed to network_write_cancel in
 * order to cancel the write.
 */
void * network_write(int, const uint8_t *, size_t, size_t,
    int (*)(void *, size_t), void *);

/**
 * network_write_cancel(cookie):
 * Cancel the buffer write for which the cookie ${cookie} was returned by
 * network_write.  Do not invoke the callback associated with the write.
 */
void network_write_cancel(void *);

#endif /* !_NETWORK_H_ */

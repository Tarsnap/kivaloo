#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include <openssl/ssl.h>

#include "network_ssl_compat.h"

#ifdef NETWORK_SSL_COMPAT_READ_WRITE_EX
/**
 * network_ssl_compat_write_ex(ssl, buf, num, written):
 * Write up to ${num} bytes from ${buf} to the ${ssl} connection.  Store the
 * number of bytes written in ${written}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * COMPATIBILITY: Behave like SSL_write_ex(), provided that the ssl
 * connection is non-blocking and has the partial-writes-allowed option turned
 * on.
 */
int
network_ssl_compat_write_ex(SSL * ssl, const void * buf, size_t num,
    size_t * written)
{
	int ret;

	/* Sanity check. */
	assert(num > 0);

	/* Reduce the number of bytes to write (if necessary). */
	if (num > INT_MAX)
		num = INT_MAX;

	/* Attempt to send data. */
	ret = SSL_write(ssl, buf, (int)num);

	if (ret > 0) {
		/* Sanity check. */
		assert(ret <= (int)num);

		/* Record the number of bytes written, and overall success. */
		*written = (size_t)ret;
		ret = 1;
	} else {
		/*
		 * Do nothing here, because ret is a meaningful value for
		 * determining the error.
		 */
	}

	return (ret);
}
#endif

#ifdef NETWORK_SSL_COMPAT_READ_WRITE_EX
/**
 * network_ssl_compat_read_ex(ssl, buf, num, readbytes):
 * Read up to ${num} bytes from the ${ssl} connection into ${buf}.  Store the
 * number of bytes read in ${readbytes}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * COMPATIBILITY: Behave like SSL_read_ex(), provided that the ssl connection
 * is non-blocking.
 */
int
network_ssl_compat_read_ex(SSL * ssl, void * buf, size_t num,
    size_t * readbytes)
{
	int ret;

	/* Sanity check. */
	assert(num > 0);

	/* Reduce the number of bytes to write (if necessary). */
	if (num > INT_MAX)
		num = INT_MAX;

	/* Attempt to read data. */
	ret = SSL_read(ssl, buf, (int)num);

	if (ret > 0) {
		/* Sanity check. */
		assert(ret <= (int)num);

		/* Record the number of bytes read, and overall success. */
		*readbytes = (size_t)ret;
		ret = 1;
	} else {
		/*
		 * Do nothing here, because ret is a meaningful value for
		 * determining the error.
		 */
	}

	return (ret);
}
#endif

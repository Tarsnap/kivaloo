#include <assert.h>
#include <stddef.h>

#include <openssl/ssl.h>

#include <openssl/err.h>

#include "warnp.h"

#include "ssl_compat.h"

#ifndef OPENSSL_VERSION_NUMBER
#error "OPENSSL_VERSION_NUMBER must be defined"
#endif

/*
 * LibreSSL claims to be OpenSSL 2.0, but (currently) has APIs compatible with
 * OpenSSL 1.0.1g.
 */
#ifdef LIBRESSL_VERSION_NUMBER
#undef OPENSSL_VERSION_NUMBER
#define OPENSSL_VERSION_NUMBER 0x1000107fL
#endif

/**
 * ssl_compat_write(ssl, buf, num, written):
 * Write ${num} bytes from ${buf} to the ${ssl} connection.  Store the number
 * of bytes written in ${written}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * NOTE: if you are using OpenSSL < 1.1.1, or libressl, the maximum value of
 * ${num} is INT_MAX.
 */
int
ssl_compat_write(SSL * ssl, const void * buf, size_t num, size_t * written)
{

	/* Sanity check. */
	assert(num > 0);

#if OPENSSL_VERSION_NUMBER < 0x10101000L
	int ret;

	/* Check if we have a valid value for the old API. */
	assert(num < INT_MAX);

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
#else
	/* Attempt to send data. */
	return (SSL_write_ex(ssl, buf, num, written));
#endif
}

/**
 * ssl_compat_read(ssl, buf, num, readbytes):
 * Read ${num} bytes from ${buf} to the ${ssl} connection.  Store the number
 * of bytes read in ${readbytes}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * NOTE: if you are using OpenSSL < 1.1.1 or libressl, the maximum value of
 * ${num} is INT_MAX.
 */
int
ssl_compat_read(SSL * ssl, void * buf, size_t num, size_t * readbytes)
{

	/* Sanity check. */
	assert(num > 0);

#if OPENSSL_VERSION_NUMBER < 0x10101000L
	int ret;

	/* Check if we have a valid value for the old API. */
	assert(num < INT_MAX);

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
#else
	/* Attempt to read data. */
	return (SSL_read_ex(ssl, buf, num, readbytes));
#endif
}

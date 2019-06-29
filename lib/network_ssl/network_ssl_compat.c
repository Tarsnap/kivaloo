#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <openssl/ssl.h>

#include "network_ssl_compat.h"

#ifdef NETWORK_SSL_COMPAT_TLS_VERSION
/**
 * network_ssl_compat_TLS_client_method():
 * Create a SSL_METHOD.
 *
 * COMPATIBILITY: Behave like TLS_client_method().
 */
const SSL_METHOD *
network_ssl_compat_TLS_client_method()
{

	return (SSLv23_client_method());
}
#endif

#ifdef NETWORK_SSL_COMPAT_TLS_VERSION
/**
 * network_ssl_compat_CTL_set_min_proto_version(ctx, version):
 * Set the minimum protocol version to ${version}.
 *
 * COMPATIBILITY: Behave like SSL_CTX_set_min_proto_version(), provided
 * that ${version} is TLS1_2_VERSION.
 */
int
network_ssl_compat_CTL_set_min_proto_version(SSL_CTX * ctx, int version)
{
	long options;

	/* This the only version currently supported in this file. */
	assert(version == TLS1_2_VERSION);

	/* Disable all protocols lower than TLS1_2_VERSION. */
	options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 |
	    SSL_OP_NO_TLSv1_1;

	/*
	 * Unfortunately the _set_options() function doesn't return success or
	 * failure; instead, it returns the new bitmask after setting the
	 * options.  So we need to AND it with the constant to verify that
	 * it's been set.
	 */
	if ((SSL_CTX_set_options(ctx, options) & options) != options)
		return (0);

	/* Success! */
	return (1);
}
#endif

#ifdef NETWORK_SSL_COMPAT_CHECK_HOSTNAME
/**
 * network_ssl_compat_set1_host(ssl, hostname):
 * Set expected name for hostname verification.
 *
 * COMPATIBILITY: Behave like SSL_set1_host().
 */
int
network_ssl_compat_set1_host(SSL * ssl, const char * hostname)
{
	X509_VERIFY_PARAM * param;

	param = SSL_get0_param(ssl);
	return (X509_VERIFY_PARAM_set1_host(param, hostname, strlen(hostname)));
}
#endif

#ifdef NETWORK_SSL_COMPAT_CHECK_HOSTNAME
/**
 * network_ssl_compat_set_hostflags(ssl, flags):
 * Set flags for hostname verification.
 *
 * COMPATIBILITY: Behave like SSL_set_hostflags().
 */
void
network_ssl_compat_set_hostflags(SSL * ssl, unsigned int flags)
{
	X509_VERIFY_PARAM * param;

	param = SSL_get0_param(ssl);
	X509_VERIFY_PARAM_set_hostflags(param, flags);
}
#endif

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

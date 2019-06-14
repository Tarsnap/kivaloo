#include <assert.h>
#include <stddef.h>

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

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
 * ssl_compat_TLS_client_method():
 * Create a SSL_METHOD suitable for TLS 1.2 or higher.
 *
 * COMPAT: if you are using OpenSSL < 1.1.0, or libressl, this only supplies
 * TLS 1.2.
 */
const SSL_METHOD *
ssl_compat_TLS_client_method()
{
	const SSL_METHOD * meth;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	assert(1==0);
#else
	if ((meth = TLS_client_method()) == NULL) {
		warn0("TLS_client_method");
		goto err0;
	}
#endif

	/* Success! */
	return (meth);

err0:
	/* Failure! */
	return (NULL);
}

/**
 * ssl_compat_ctx_min_tls_1_2(ctx):
 * Set the minimum protocol version of the SSL context ${ctx} to TLS 1.2.
 *
 * Return 0 on success, -1 on failure.
 */
int
ssl_compat_ctx_min_TLS_1_2(SSL_CTX * ctx)
{

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	assert(1==0);
#else
	if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
		warn0("Could not set minimum TLS version");
		goto err0;
	}
#endif

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * ssl_compat_enable_hostname_validation(ssl, hostname):
 * Enable hostname validation to occur when establishing an SSL connection.
 */
int
ssl_compat_enable_hostname_validation(SSL * ssl, const char * hostname)
{

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	assert(1==0);
#else
	/* Tell OpenSSL which host we're trying to talk to... */
	if (!SSL_set1_host(ssl, hostname)) {
		warn0("SSL_set1_host");
		goto err0;
	}

	/* ... and ask it to make sure that this is what is happening. */
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
#endif

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * ssl_compat_write(ssl, buf, num, written):
 * Write ${num} bytes from ${buf} to the ${ssl} connection.  Store the number
 * of bytes written in ${written}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * COMPAT: if you are using OpenSSL < 1.1.1, or libressl, the maximum value of
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
 * COMPAT: if you are using OpenSSL < 1.1.1 or libressl, the maximum value of
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

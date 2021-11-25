#ifndef _NETWORK_SSL_COMPAT_H
#define _NETWORK_SSL_COMPAT_H

#include <stddef.h>

#include <openssl/ssl.h>

/* Ensure we have a version number. */
#ifndef OPENSSL_VERSION_NUMBER
#error "OPENSSL_VERSION_NUMBER must be defined"
#endif

/* Which library are we using? */
#ifdef LIBRESSL_VERSION_NUMBER
/* LibreSSL claims to be OpenSSL 2.0; ignore that. */
#undef OPENSSL_VERSION_NUMBER

#else /* end LibreSSL section */

/* Compatibility for OpenSSL. */
#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
/* Compatibility for OpenSSL 1.1.1+: nothing needed. */

#elif OPENSSL_VERSION_NUMBER >= 0x1010000fL
/* Compatibility for OpenSSL 1.1.0. */
#define NETWORK_SSL_COMPAT_READ_WRITE_EX

#else
/* Compatibility for OpenSSL pre-1.1.0. */
#define NETWORK_SSL_COMPAT_TLS_CLIENT_METHOD
#define NETWORK_SSL_COMPAT_SET_MIN_PROTO_VERSION
#define NETWORK_SSL_COMPAT_CHECK_HOSTNAME
#define NETWORK_SSL_COMPAT_READ_WRITE_EX

#endif /* End of OpenSSL compatibility section. */
#endif

#ifdef NETWORK_SSL_COMPAT_TLS_CLIENT_METHOD
/**
 * network_ssl_compat_TLS_client_method(void):
 * Create a SSL_METHOD.
 *
 * COMPATIBILITY: Behave like TLS_client_method().
 */
const SSL_METHOD * network_ssl_compat_TLS_client_method(void);
#endif

#ifdef NETWORK_SSL_COMPAT_SET_MIN_PROTO_VERSION
/**
 * network_ssl_compat_CTL_set_min_proto_version(ctx, version):
 * Set the minimum protocol version to ${version}.
 *
 * COMPATIBILITY: Behave like SSL_CTX_set_min_proto_version(), provided
 * that ${version} is TLS1_2_VERSION.
 */
int network_ssl_compat_CTL_set_min_proto_version(SSL_CTX *, int);
#endif

#ifdef NETWORK_SSL_COMPAT_CHECK_HOSTNAME
/**
 * network_ssl_compat_set1_host(ssl, hostname):
 * Set expected name for hostname verification.
 *
 * COMPATIBILITY: Behave like SSL_set1_host().
 */
int network_ssl_compat_set1_host(SSL *, const char *);
#endif

#ifdef NETWORK_SSL_COMPAT_CHECK_HOSTNAME
/**
 * network_ssl_compat_set_hostflags(ssl, flags):
 * Set flags for hostname verification.
 *
 * COMPATIBILITY: Behave like SSL_set_hostflags().
 */
void network_ssl_compat_set_hostflags(SSL *, unsigned int);
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
int network_ssl_compat_write_ex(SSL *, const void *, size_t, size_t *);
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
int network_ssl_compat_read_ex(SSL *, void *, size_t, size_t *);
#endif

#endif /* !_NETWORK_SSL_COMPAT_H */

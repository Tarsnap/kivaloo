#ifndef _NETWORK_SSL_COMPAT_H
#define _NETWORK_SSL_COMPAT_H

#include <stddef.h>

#include <openssl/ssl.h>

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

/* Compatibility for OpenSSL pre-1.1.1. */
#if OPENSSL_VERSION_NUMBER < 0x10101000L
#define NETWORK_SSL_COMPAT_READ_WRITE_EX
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

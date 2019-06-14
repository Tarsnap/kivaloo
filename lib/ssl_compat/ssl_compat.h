#ifndef _SSL_COMPAT_H_
#define _SSL_COMPAT_H_

#include <stddef.h>

#include <openssl/ssl.h>

/**
 * ssl_compat_TLS_client_method():
 * Create a SSL_METHOD suitable for TLS 1.2 or higher.
 *
 * COMPAT: if you are using OpenSSL < 1.1.0, or libressl, this only supplies
 * TLS 1.2.
 */
const SSL_METHOD * ssl_compat_TLS_client_method(void);

/**
 * ssl_compat_ctx_min_tls_1_2(ctx):
 * Set the minimum protocol version of the SSL context ${ctx} to TLS 1.2.
 *
 * Return 0 on success, -1 on failure.
 */
int ssl_compat_ctx_min_TLS_1_2(SSL_CTX *);

/**
 * ssl_compat_enable_hostname_validation(ssl, hostname):
 * Enable hostname validation to occur when establishing an SSL connection.
 */
int ssl_compat_enable_hostname_validation(SSL *, const char *);

/**
 * ssl_compat_write(ssl, buf, num, written):
 * Write ${num} bytes from ${buf} to the ${ssl} connection.  Store the number
 * of bytes written in ${written}.
 *
 * On success, return 1.  On failure, return 0 or a negative value (for some
 * versions of OpenSSL, the exact value must be passed to SSL_get_err() in
 * order to receive a meaningful error code).
 *
 * COMPAT: if you are using OpenSSL < 1.1.1 or libressl, the maximum value of
 * ${num} is INT_MAX.
 */
int ssl_compat_write(SSL *, const void *, size_t, size_t *);

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
int ssl_compat_read(SSL *, void *, size_t, size_t *);

#endif /* !_SSL_COMPAT_H_ */

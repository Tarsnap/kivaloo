#include "network_ssl.h"

#include "netbuf.h"

/*
 * Function pointers defined in netbuf_read and netbuf_write; we set them
 * from our _init functions in order to avoid unnecessary linkage.
 */
extern void * (* netbuf_read_ssl_func)(struct network_ssl_ctx *, uint8_t *,
    size_t, size_t, int (*)(void *, ssize_t), void *);
extern void (* netbuf_read_ssl_cancel_func)(void *);
extern void * (* netbuf_write_ssl_func)(struct network_ssl_ctx *,
    const uint8_t *, size_t, size_t, int (*)(void *, ssize_t), void *);
extern void (* netbuf_write_ssl_cancel_func)(void *);

/**
 * netbuf_ssl_read_init(ssl):
 * Behave as netbuf_read_init but take an SSL context instead.
 */
struct netbuf_read *
netbuf_ssl_read_init(struct network_ssl_ctx * ssl)
{

	/* Set function pointers. */
	netbuf_read_ssl_func = network_ssl_read;
	netbuf_read_ssl_cancel_func = network_ssl_read_cancel;

	/* Create the netbuf reader. */
	return (netbuf_read_init2(-1, ssl));
}

/**
 * netbuf_ssl_write_init(ssl, fail_callback, fail_cookie):
 * Behave as netbuf_write_init but take an SSL context instead.
 */
struct netbuf_write *
netbuf_ssl_write_init(struct network_ssl_ctx * ssl,
    int (* fail_callback)(void *), void * fail_cookie)
{

	/* Set function pointers. */
	netbuf_write_ssl_func = network_ssl_write;
	netbuf_write_ssl_cancel_func = network_ssl_write_cancel;

	/* Create the netbuf writer. */
	return (netbuf_write_init2(-1, ssl, fail_callback, fail_cookie));
}

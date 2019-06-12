#include <string.h>
#include <stdlib.h>

#include "netbuf.h"
#include "network_ssl.h"

#include "http.h"

/*
 * Function pointers defined in http.c; we set them from https_request in
 * order to avoid requiring unencrypted HTTP code to link to libssl.
 */
extern struct network_ssl_ctx * (* network_ssl_open_func)(int, const char *);
extern void (* network_ssl_close_func)(struct network_ssl_ctx *);
extern struct netbuf_read *
    (* netbuf_ssl_read_init_func)(struct network_ssl_ctx *);
extern struct netbuf_write * (* netbuf_ssl_write_init_func)(struct network_ssl_ctx *,
    int (*)(void *), void *);

/**
 * https_request(addrs, request, maxrlen, callback, cookie, hostname):
 * Behave as http_request, but use HTTPS and verify that the target host is
 * ${hostname}.
 */
void *
https_request(struct sock_addr * const * addrs, struct http_request * request,
    size_t maxrlen, int (* callback)(void *, struct http_response *),
    void * cookie, const char * hostname)
{
	struct http_cookie * H;
	char * sslhost;

	/* Set function pointers. */
	network_ssl_open_func = network_ssl_open;
	network_ssl_close_func = network_ssl_close;
	netbuf_ssl_read_init_func = netbuf_ssl_read_init;
	netbuf_ssl_write_init_func = netbuf_ssl_write_init;

	/* Duplicate the hostname. */
	if ((sslhost = strdup(hostname)) == NULL)
		goto err0;

	/* Create an HTTP state. */
	if ((H = http_request2(addrs, request, maxrlen, callback,
	    cookie, sslhost)) == NULL)
		goto err1;

	/* Success! */
	return (H);

err1:
	free(sslhost);
err0:
	/* Failure! */
	return (NULL);
}

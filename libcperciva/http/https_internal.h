#ifndef HTTPS_INTERNAL_H_
#define HTTPS_INTERNAL_H_

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
 * http_request2(addrs, request, maxrlen, callback, cookie, sslhost):
 * Behave like http_request if ${sslhost} is NULL.  If ${sslhost} is not NULL,
 * send the request via HTTPS.
 */
void *
http_request2(struct sock_addr * const *, struct http_request *, size_t,
    int (*)(void *, struct http_response *), void *, char *);

#endif /* !HTTPS_INTERNAL_H_ */

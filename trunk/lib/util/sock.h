#ifndef _SOCK_H_
#define _SOCK_H_

/**
 * Address strings are of the following forms:
 * /path/to/unix/socket
 * [ip.v4.ad.dr]:port
 * [ipv6:add::ress]:port
 * host.name:port
 */

/* Opaque address structure. */
struct sock_addr;

/**
 * sock_resolve(addr):
 * Return a NULL-terminated array of pointers to sock_addr structures.
 */
struct sock_addr ** sock_resolve(const char *);

/**
 * sock_listener(sa):
 * Create a socket, set SO_REUSEADDR, bind it to the socket address ${sa},
 * mark it for listening, and mark it as non-blocking.
 */
int sock_listener(const struct sock_addr *);

/**
 * sock_connect(sas):
 * Iterate through the addresses in ${sas}, attempting to create a socket and
 * connect (blockingly).  Once connected, stop iterating, mark the socket as
 * non-blocking, and return it.
 */
int sock_connect(struct sock_addr * const *);

/**
 * sock_connect_nb(sa):
 * Create a socket, mark it as non-blocking, and attempt to connect to the
 * address ${sa}.  Return the socket (connected or in the process of
 * connecting) or -1 on error.
 */
int sock_connect_nb(const struct sock_addr *);

/**
 * sock_addr_cmp(sa1, sa2):
 * Return non-zero iff the socket addresses ${sa1} and ${sa2} are different.
 */
int sock_addr_cmp(const struct sock_addr *, const struct sock_addr *);

/**
 * sock_addr_dup(sa):
 * Duplicate the provided socket address.
 */
struct sock_addr * sock_addr_dup(const struct sock_addr *);

/**
 * sock_addr_serialize(sa, buf, buflen):
 * Allocate a buffer and serialize the socket address ${sa} into it.  Return
 * the buffer via ${buf} and its length via ${buflen}.  The serialization is
 * machine and operating system dependent.
 */
int sock_addr_serialize(const struct sock_addr *, uint8_t **, size_t *);

/**
 * sock_addr_deserialize(buf, buflen):
 * Deserialize the ${buflen}-byte serialized socket address from ${buf}.
 */
struct sock_addr * sock_addr_deserialize(const uint8_t *, size_t);

/**
 * sock_addr_free(sa):
 * Free the provided sock_addr structure.
 */
void sock_addr_free(struct sock_addr *);

/**
 * sock_addr_freelist(sas):
 * Free the provided NULL-terminated array of sock_addr structures.
 */
void sock_addr_freelist(struct sock_addr **);

#endif /* !_SOCK_H_ */

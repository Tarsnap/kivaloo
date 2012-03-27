#ifndef _S3_SERVERPOOL_H_
#define _S3_SERVERPOOL_H_

/* Opaque type. */
struct s3_serverpool;
struct sock_addr;

/**
 * s3_serverpool_init(void):
 * Create a pool of S3 servers.
 */
struct s3_serverpool * s3_serverpool_init(void);

/**
 * s3_serverpool_add(SP, sa, ttl):
 * Add the address ${sa} to the server pool ${SP} for the next ${ttl} seconds.
 * (If already in the pool, update the expiry time.)
 */
int s3_serverpool_add(struct s3_serverpool *, const struct sock_addr *, int);

/**
 * s3_serverpool_pick(SP):
 * Pick an address from ${SP} and return it.  The caller is responsible for
 * freeing the address.
 */
struct sock_addr * s3_serverpool_pick(struct s3_serverpool *);

/**
 * s3_serverpool_free(SP):
 * Free the server pool ${SP}.
 */
void s3_serverpool_free(struct s3_serverpool *);

#endif /* !_S3_SERVERPOOL_H_ */

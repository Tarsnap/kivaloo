#ifndef _DNS_H_
#define _DNS_H_

/* Opaque types. */
struct s3_request_queue;

/**
 * dns_reader_start(Q, target):
 * Start performing DNS lookups for ${target}, feeding resulting addresses
 * into ${Q}.  Return a cookie which can be passed to dns_reader_stop.
 */
struct dns_reader * dns_reader_start(struct s3_request_queue *, const char *);

/**
 * dns_reader_stop(DR):
 * Stop the DNS reader ${DR}.
 */
void dns_reader_stop(struct dns_reader *);

#endif /* !_DNS_H_ */

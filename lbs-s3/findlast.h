#ifndef FINDLAST_H_
#define FINDLAST_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque types. */
struct wire_requestqueue;

/**
 * findlast(Q_S3, bucket, L, olen):
 * Using the S3 daemon connected to ${Q_S3}, find the number of the last
 * (non-empty) object in the S3 bucket ${bucket} and store it to ${L}.  Store
 * the size of that object into ${olen}.  If there are no numbered objects,
 * return L = 0, olen = 0.
 *
 * This function may call events_run() internally.
 */
int findlast(struct wire_requestqueue *, const char *, uint64_t *, size_t *);

#endif /* !FINDLAST_H_ */

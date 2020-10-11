#ifndef _CAPACITY_H_
#define _CAPACITY_H_

/* Opaque types. */
struct serverpool;
struct dynamodb_request_queue;

/**
 * capacity_init(key_id, key_secret, tname, rname, SP, QW, QR):
 * Using the AWS key id ${key_id} and secret key ${key_secret}, issue
 * DescribeTable requests to the DynamoDB table ${tname} in AWS region
 * ${rname}, using endpoints returned by the server pool ${SP}.  Update the
 * capacity of the write queue ${QW} and read queue ${QR}.
 *
 * Issue one request immediately, and wait for it to complete before
 * returning; issue subsequent requests every 15 seconds.
 *
 * This function may call events_run() internally.
 */
struct capacity_reader * capacity_init(const char *, const char *,
    const char *, const char *, struct serverpool *,
    struct dynamodb_request_queue *, struct dynamodb_request_queue *);

/**
 * capacity_free(M):
 * Stop issuing DescribeTable requests.
 */
void capacity_free(struct capacity_reader *);

#endif /* !_CAPACITY_H_ */

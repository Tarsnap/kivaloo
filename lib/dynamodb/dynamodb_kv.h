#ifndef _DYNAMODB_KV_
#define _DYNAMODB_KV_

#include <stddef.h>
#include <stdint.h>

/**
 * dynamodb_kv_put(table, key, buf, len):
 * Construct a DynamoDB request body for a PutItem of V=${buf} (of length
 * ${len}) associated with K=${key} in DynamoDB table ${table}.
 */
char * dynamodb_kv_put(const char *, const char *, const uint8_t *, size_t);

/**
 * dynamodb_kv_icas(table, key, buf, len, buf2, len2):
 * Construct a DynamoDB request body for an ICAS replacing V=${buf} (of length
 * ${len}) with V=${buf2} (of length ${len2}), associated with K=${key} in
 * DynamoDB table ${table}.
 */
char * dynamodb_kv_icas(const char *, const char *, const uint8_t *, size_t,
    const uint8_t *, size_t);

/**
 * dynamodb_kv_create(table, key, buf, len):
 * Construct a DynamoDB request body for a PutItem of V=${buf} (of length
 * ${len}) associated with K=${key} in DynamoDB table ${table}, with a
 * precondition that the value does not exist or is not changing.
 */
char * dynamodb_kv_create(const char *, const char *, const uint8_t *, size_t);

/**
 * dynamodb_kv_get(table, key):
 * Construct a DynamoDB request body for a GetItem associated with K=${key}
 * in DynamoDB table ${table}.
 */
char * dynamodb_kv_get(const char *, const char *);

/**
 * dynamodb_kv_getc(table, key):
 * Construct a DynamoDB request body for a GetItem associated with K=${key}
 * in DynamoDB table ${table}, with strong consistency.
 */
char * dynamodb_kv_getc(const char *, const char *);

/**
 * dynamodb_kv_delete(table, key):
 * Construct a DynamoDB request body for a DeleteItem associated with
 * K=${key} in DynamoDB table ${table}.
 */
char * dynamodb_kv_delete(const char *, const char *);

/**
 * dynamodb_kv_extractv(inbuf, inlen, outbuf, outlen):
 * Extract and base64 decode the "V" field in the GetItem response provided
 * via ${inbuf} (of length ${inlen}).  Return a buffer and its length via
 * ${outbuf} / ${outlen}.  If there is no such field, return with ${outbuf}
 * set to NULL.
 */
int dynamodb_kv_extractv(const uint8_t *, size_t, uint8_t **, uint32_t *);

#endif /* !_DYNAMODB_KV_ */

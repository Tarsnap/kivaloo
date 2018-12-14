#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "b64encode.h"
#include "json.h"

#include "dynamodb_kv.h"

/* Helper macro for constructing strings. */
#define COPYANDINCR(s, spos, s1) do {	\
	memcpy(&s[spos], s1, strlen(s1));	\
	spos += strlen(s1);			\
} while (0);

/**
 * dynamodb_kv_put(table, key, buf, len):
 * Construct a DynamoDB request body for a PutItem of V=${buf} (of length
 * ${len}) associated with K=${key} in DynamoDB table ${table}.
 */
char *
dynamodb_kv_put(const char * table, const char * key,
    const uint8_t * buf, size_t len)
{
	/**
	 * The body of a PutItem request (with spaces added) is:
	 * { "TableName": "TABLE",
	 *   "Item": {
	 *     "K": { "S": "KEY" },
	 *     "V": { "B": "BASE64VALUE" }
	 *   },
	 *   "ReturnConsumedCapacity": "TOTAL"
	 * }
	 */
	const char * s1 = "{\"TableName\":\"";
	const char * s2 = "\",\"Item\":{\"K\":{\"S\":\"";
	const char * s3 = "\"},\"V\":{\"B\":\"";
	const char * s4 = "\"}},\"ReturnConsumedCapacity\":\"TOTAL\"}";
	size_t slen = strlen(s1) + strlen(table) + strlen(s2) + strlen(key) +
	    strlen(s3) + ((len + 2) / 3) * 4 + strlen(s4);
	size_t spos = 0;
	char * s;

	/* Allocate request string. */
	if ((s = malloc(slen + 1)) == NULL)
		goto done;

	/* Construct the request, piece by piece. */
	COPYANDINCR(s, spos, s1);
	COPYANDINCR(s, spos, table);
	COPYANDINCR(s, spos, s2);
	COPYANDINCR(s, spos, key);
	COPYANDINCR(s, spos, s3);
	b64encode(buf, &s[spos], len);
	spos += ((len + 2) / 3) * 4;
	COPYANDINCR(s, spos, s4);
	s[spos] = '\0';

	/* Check that we got the buffer size right. */
	assert(slen == spos);

done:
	/* Return string (or NULL if allocation failed). */
	return (s);
}

/**
 * dynamodb_kv_get(table, key):
 * Construct a DynamoDB request body for a GetItem associated with K=${key}
 * in DynamoDB table ${table}.
 */
char *
dynamodb_kv_get(const char * table, const char * key)
{
	/**
	 * The body of a GetItem request (with spaces added) is:
	 * { "TableName": "TABLE",
	 *   "Key": {
	 *     "K": { "S": "KEY" }
	 *   },
	 *   "ReturnConsumedCapacity": "TOTAL"
	 * }
	 */
	const char * s1 = "{\"TableName\":\"";
	const char * s2 = "\",\"Key\":{\"K\":{\"S\":\"";
	const char * s3 = "\"}},\"ReturnConsumedCapacity\":\"TOTAL\"}";
	size_t slen = strlen(s1) + strlen(table) + strlen(s2) + strlen(key) +
	    strlen(s3);
	size_t spos = 0;
	char * s;

	/* Allocate request string. */
	if ((s = malloc(slen + 1)) == NULL)
		goto done;

	/* Construct the request, piece by piece. */
	COPYANDINCR(s, spos, s1);
	COPYANDINCR(s, spos, table);
	COPYANDINCR(s, spos, s2);
	COPYANDINCR(s, spos, key);
	COPYANDINCR(s, spos, s3);
	s[spos] = '\0';

	/* Check that we got the buffer size right. */
	assert(slen == spos);

done:
	/* Return string (or NULL if allocation failed). */
	return (s);
}

/**
 * dynamodb_kv_getc(table, key):
 * Construct a DynamoDB request body for a GetItem associated with K=${key}
 * in DynamoDB table ${table}, with strong consistency.
 */
char *
dynamodb_kv_getc(const char * table, const char * key)
{
	/**
	 * The body of a strongly consistent GetItem request (with spaces
	 * added) is:
	 * { "ConsistentRead": true,
	 *   "TableName": "TABLE",
	 *   "Key": {
	 *     "K": { "S": "KEY" }
	 *   },
	 *   "ReturnConsumedCapacity": "TOTAL"
	 * }
	 */
	const char * s1 = "{\"ConsistentRead\":true,\"TableName\":\"";
	const char * s2 = "\",\"Key\":{\"K\":{\"S\":\"";
	const char * s3 = "\"}},\"ReturnConsumedCapacity\":\"TOTAL\"}";
	size_t slen = strlen(s1) + strlen(table) + strlen(s2) + strlen(key) +
	    strlen(s3);
	size_t spos = 0;
	char * s;

	/* Allocate request string. */
	if ((s = malloc(slen + 1)) == NULL)
		goto done;

	/* Construct the request, piece by piece. */
	COPYANDINCR(s, spos, s1);
	COPYANDINCR(s, spos, table);
	COPYANDINCR(s, spos, s2);
	COPYANDINCR(s, spos, key);
	COPYANDINCR(s, spos, s3);
	s[spos] = '\0';

	/* Check that we got the buffer size right. */
	assert(slen == spos);

done:
	/* Return string (or NULL if allocation failed). */
	return (s);
}

/**
 * dynamodb_kv_delete(table, key):
 * Construct a DynamoDB request body for a DeleteItem associated with
 * K=${key} in DynamoDB table ${table}.
 */
char *
dynamodb_kv_delete(const char * table, const char * key)
{
	/**
	 * The body of a DeleteItem request (with spaces added) is:
	 * { "TableName": "TABLE",
	 *   "Key": {
	 *     "K": { "S": "KEY" }
	 *   },
	 *   "ReturnConsumedCapacity": "TOTAL"
	 * }
	 */
	const char * s1 = "{\"TableName\":\"";
	const char * s2 = "\",\"Key\":{\"K\":{\"S\":\"";
	const char * s3 = "\"}},\"ReturnConsumedCapacity\":\"TOTAL\"}";
	size_t slen = strlen(s1) + strlen(table) + strlen(s2) + strlen(key) +
	    strlen(s3);
	size_t spos = 0;
	char * s;

	/* Allocate request string. */
	if ((s = malloc(slen + 1)) == NULL)
		goto done;

	/* Construct the request, piece by piece. */
	COPYANDINCR(s, spos, s1);
	COPYANDINCR(s, spos, table);
	COPYANDINCR(s, spos, s2);
	COPYANDINCR(s, spos, key);
	COPYANDINCR(s, spos, s3);
	s[spos] = '\0';

	/* Check that we got the buffer size right. */
	assert(slen == spos);

done:
	/* Return string (or NULL if allocation failed). */
	return (s);
}

/**
 * dynamodb_kv_extractv(inbuf, inlen, outbuf, outlen):
 * Extract and base64 decode the "V" field in the GetItem response provided
 * via ${inbuf} (of length ${inlen}).  Return a buffer and its length via
 * ${outbuf} / ${outlen}.  If there is no such field, return with ${outbuf}
 * set to NULL.
 */
int
dynamodb_kv_extractv(const uint8_t * inbuf, size_t inlen,
    uint8_t ** outbuf, uint32_t * outlen)
{
	const uint8_t * p = inbuf;
	const uint8_t * end = &inbuf[inlen];
	size_t slen;
	size_t vlen;

	/* If we have no response body, there is no value. */
	if (inbuf == NULL)
		goto novalue;

	/**
	 * We need to locate B64VALUE in the string
	 * {"Item":{"V":{"B":"dmFsdWUK"},"K":{"S":"key"}}}
	 * so we look for the json object associated with "Item"; then look
	 * for the json object associated with "V" inside that; then look
	 * for the json object associated with "B" inside that.
	 */
	p = json_find(p, end, "Item");
	p = json_find(p, end, "V");
	p = json_find(p, end, "B");

	/* We should be pointing at the opening '"' of a string. */
	if (p == end)
		goto novalue;
	if (*p++ != '"')
		goto novalue;

	/* How long is it? */
	for (slen = 0; &p[slen] != end; slen++) {
		if (p[slen] == '"')
			break;
	}

	/* We should have found a terminating '"'. */
	if (&p[slen] == end)
		goto novalue;

	/* Allocate a buffer. */
	if ((*outbuf = malloc((slen / 4) * 3)) == NULL)
		goto err0;

	/* Attempt to parse the base64-encoded data. */
	if (b64decode((const char *)p, slen, *outbuf, &vlen))
		goto novalue1;

	/* Record the size of the returned value. */
	if (vlen >= (uint32_t)(-1))
		goto novalue1;
	*outlen = vlen;

	/* Success! */
	return (0);

novalue1:
	free(*outbuf);
novalue:
	/* We have no value. */
	*outbuf = NULL;

	/* Success!  (Or at least, no internal error.) */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

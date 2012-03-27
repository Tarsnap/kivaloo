#include <string.h>

#include "hexify.h"
#include "http.h"
#include "md5.h"

#include "s3_verifyetag.h"

/**
 * s3_verifyetag(res):
 * Check if the HTTP response ${res} contains an ETag header which matches its
 * data.  Return 1 if yes, or 0 if not (i.e., if either there is no ETag, or
 * it does not match the data).
 */
int
s3_verifyetag(struct http_response * res)
{
	const char * etag;
	uint8_t etagmd5[16];
	uint8_t datamd5[16];

	/* Look for an ETag header. */
	etag = http_findheader(res->headers, res->nheaders, "ETag");

	/* If there is no header, return zero. */
	if (etag == NULL)
		return (0);

	/* Skip any leading whitespace. */
	while ((etag[0] == ' ') || (etag[0] == '\t'))
		etag++;

	/* It should be '"' <32 characters of hex> '"'. */
	if ((strlen(etag) != 34) || (etag[0] != '"') || (etag[33] != '"'))
		return (0);

	/* Characters 1--33 should be hex.  Parse them. */
	if (unhexify(&etag[1], etagmd5, 16))
		return (0);

	/* Compute the MD5 hash of the HTTP response body. */
	MD5_Buf(res->body, res->bodylen, datamd5);

	/* Check if the MD5 hash matches the parsed ETag. */
	if (memcmp(etagmd5, datamd5, 16))
		return (0);
	else
		return (1);
}

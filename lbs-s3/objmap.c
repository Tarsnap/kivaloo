#include <stdint.h>

#include "hexify.h"
#include "md5.h"
#include "sysendian.h"

#include "objmap.h"

/**
 * objmap(N):
 * Convert the S3 object number ${N} into an object name.  Return a statically
 * allocated string which is valid until the next call to objmap().
 */
const char *
objmap(uint64_t N)
{
	uint8_t Nbuf[8];
	uint8_t hbuf[16];
	static char oname[22]; /* HASH_0123456789012345 */

	/* Serialize the value N. */
	be64enc(Nbuf, N);

	/* Hash the (serialized) value. */
	MD5_Buf(Nbuf, 8, hbuf);

	/* Hexify the first two bytes of hash. */
	hexify(hbuf, &oname[0], 2);

	/* Separator character. */
	oname[4] = '_';

	/* Hexify the 8-byte integer. */
	hexify(Nbuf, &oname[5], 8);

	/* NUL-terminate. */
	oname[21] = '\0';

	/* Return the constructed string. */
	return (oname);
}

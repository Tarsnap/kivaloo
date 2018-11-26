#include <stdint.h>

#include "hexify.h"
#include "sysendian.h"

#include "objmap.h"

/**
 * objmap(N):
 * Convert the page number ${N} into a (string) key.  Return a statically
 * allocated string which is valid until the next call to objmap.
 */
const char *
objmap(uint64_t N)
{
	uint8_t Nbuf[8];
	static char key[17]; /* 0123456789ABCDEF\0 */

	/* Serialize the value N. */
	be64enc(Nbuf, N);

	/* Hexify the 8-byte integer. */
	hexify(Nbuf, key, 8);

	/* Return the constructed string. */
	return (key);
}

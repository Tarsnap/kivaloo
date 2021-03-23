#include <stdint.h>

#include "sysendian.h"

#include "sha256.h"

#include "mkpair.h"

/**
 * mkkey(X, Y, buf):
 * Write the 40-byte key
 * offset  length
 * ------  ------
 *    0       8    64-bit big-endian X
 *    8      32    sha256(\000.(64-bit big-endian X).(64-bit big-endian Y))
 * into ${buf}.
 */
void
mkkey(uint64_t X, uint64_t Y, uint8_t * buf)
{
	SHA256_CTX ctx;
	uint8_t hbuf[17];

	/* Store big-endian X. */
	be64enc(&buf[0], X);

	/* Generate \000.X.Y. */
	hbuf[0] = 0;
	be64enc(&hbuf[1], X);
	be64enc(&hbuf[9], Y);

	/* Compute sha256(\000.X.Y). */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, hbuf, 17);
	SHA256_Final(&buf[8], &ctx);
}

/**
 * mkval(X, Y, buf):
 * Write the 40-byte value
 * offset  length
 * ------  ------
 *    0       8    64-bit big-endian X * 2^16 + Y
 *    8      32    sha256(\001.(64-bit big-endian X).(64-bit big-endian Y))
 * into ${buf}.
 */
void
mkval(uint64_t X, uint64_t Y, uint8_t * buf)
{
	SHA256_CTX ctx;
	uint8_t hbuf[17];

	/* Store big-endian X * 2^16 + Y. */
	be64enc(&buf[0], (X << 16) + Y);

	/* Generate \001.X.Y. */
	hbuf[0] = 0;
	be64enc(&hbuf[1], X);
	be64enc(&hbuf[9], Y);

	/* Compute sha256(\001.X.Y). */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, hbuf, 17);
	SHA256_Final(&buf[8], &ctx);
}

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cpusupport.h"
#include "crc32c_sse42.h"
#include "warnp.h"

#include "crc32c.h"

/**
 * CRC32C tables:
 * T[0][i] = reverse32(reverse8(i) * x^32 mod p(x) mod 2)
 * T[1][i] = reverse32(reverse8(i) * x^40 mod p(x) mod 2)
 * T[2][i] = reverse32(reverse8(i) * x^48 mod p(x) mod 2)
 * T[3][i] = reverse32(reverse8(i) * x^56 mod p(x) mod 2)
 */
static uint32_t T0[256];
static uint32_t T1[256];
static uint32_t T2[256];
static uint32_t T3[256];

/* Optimization: Precomputed value of T[0][0x80]. */
#define T_0_0x80 0x82f63b78

/**
 * reverse(x):
 * Return x with reversed bit-order.
 */
static uint32_t
reverse(uint32_t x)
{

	x = ((x & 0xffff0000) >> 16) | ((x & 0x0000ffff) << 16);
	x = ((x & 0xff00ff00) >> 8)  | ((x & 0x00ff00ff) << 8);
	x = ((x & 0xf0f0f0f0) >> 4)  | ((x & 0x0f0f0f0f) << 4);
	x = ((x & 0xcccccccc) >> 2)  | ((x & 0x33333333) << 2);
	x = ((x & 0xaaaaaaaa) >> 1)  | ((x & 0x55555555) << 1);

	return (x);
}

static uint32_t
times256(uint32_t r)
{
	int k;

	/* Shift left one bit at a time. */
	for (k = 0; k < 8; k++) {
		if (r & 0x80000000)
			r = (r << 1) ^ 0x1EDC6F41;
		else
			r = (r << 1);
	}

	return (r);
}

/**
 * init(void):
 * Initialize tables.
 */
static void
init(void)
{
	size_t i;
	uint32_t r;

	/* Fill in tables. */
	for (i = 0; i < 256; i++) {
		r = reverse((uint32_t)i);
		T0[i] = reverse(r = times256(r));
		T1[i] = reverse(r = times256(r));
		T2[i] = reverse(r = times256(r));
		T3[i] = reverse(r = times256(r));
	}

	/* Make sure we optimized correctly. */
	assert(T0[0x80] == T_0_0x80);
}

#ifdef CPUSUPPORT_X86_CRC32
static struct crc32_test {
	const char * buf;
	const uint8_t crc[4];
} testcase = {
	.buf = "hello world",
	.crc = { 0xca, 0x13, 0x0b, 0xaa }
};

/* Test whether CRC extensions and software code produce the same results. */
static int
crctest(void)
{
	uint32_t state = T_0_0x80;

	/* Test hardware transform function. */
	state = CRC32C_Update_SSE42(state, (const uint8_t *)testcase.buf,
	    strlen(testcase.buf));

	/* Is the output correct? */
	return (memcmp(&state, testcase.crc, 4));
}

/* Should we use CRC? */
static int
usecrc(void)
{
	static int crcgood = -1;

	/* If we haven't decided which code to use yet, decide now. */
	while (crcgood == -1) {
		/* Default to software. */
		crcgood = 0;

		/* If the CPU doesn't claim to support SSE4.2, stop here. */
		if (!cpusupport_x86_crc32())
			break;

		/* Calculate with hardware and compare against a test vector. */
		if (crctest()) {
			warn0("Disabling hardware CRC due to failed self-test");
			break;
		}

		/* CRC works; use it. */
		crcgood = 1;
	}

	return (crcgood);
}
#endif /* !CPUSUPPORT_X86_CRC32 */

/**
 * CRC32C_Init(ctx):
 * Initialize a CRC32C-computing context.
 */
void
CRC32C_Init(CRC32C_CTX * ctx)
{
	static int initdone = 0;

	/* Initialize tables. */
	if (initdone == 0) {
		init();
		initdone = 1;
	}

	/* Set state to the CRC of the implicit leading 1 bit. */
	ctx->state = T_0_0x80;
}

/**
 * CRC32C_Update(ctx, buf, len):
 * Feed ${len} bytes from the buffer ${buf} into the CRC32C being computed
 * via the context ${ctx}.
 */
void
CRC32C_Update(CRC32C_CTX * ctx, const uint8_t * buf, size_t len)
{

#ifdef CPUSUPPORT_X86_CRC32
	if (usecrc() && (len >= 8)) {
		ctx->state = CRC32C_Update_SSE42(ctx->state, buf, len);
		return;
	}
#endif

	/* Handle blocks of 4 bytes. */
	for (; len >= 4; len -= 4, buf += 4) {
		ctx->state =
		    T0[((ctx->state >> 24) & 0xff) ^ buf[3]] ^
		    T1[((ctx->state >> 16) & 0xff) ^ buf[2]] ^
		    T2[((ctx->state >> 8)  & 0xff) ^ buf[1]] ^
		    T3[((ctx->state)       & 0xff) ^ buf[0]];
	}

	/* Handle individual bytes. */
	for (; len > 0; len--, buf++) {
		ctx->state = (ctx->state >> 8) ^
		    T0[((ctx->state) & 0xff) ^ buf[0]];
	}
}

/**
 * CRC32C_Final(cbuf, ctx):
 * Store in ${cbuf} a value such that 1[buf][buf]...[buf][cbuf], where each
 * buffer is interpreted as a bit sequence starting with the least
 * significant bit of the byte in the lowest address, is a product of the
 * Castagnoli polynomial.
 */
void
CRC32C_Final(uint8_t cbuf[4], CRC32C_CTX * ctx)
{

	/* Copy state out. */
	cbuf[0] = ctx->state & 0xff;
	cbuf[1] = (ctx->state >> 8) & 0xff;
	cbuf[2] = (ctx->state >> 16) & 0xff;
	cbuf[3] = (ctx->state >> 24) & 0xff;
}

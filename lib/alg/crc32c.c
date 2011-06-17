#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "crc32c.h"

/**
 * CRC32C tables:
 * T[0][i] = reverse32(reverse8(i) * x^32 mod p(x) mod 2)
 * T[1][i] = reverse32(reverse8(i) * x^40 mod p(x) mod 2)
 * T[2][i] = reverse32(reverse8(i) * x^48 mod p(x) mod 2)
 * T[3][i] = reverse32(reverse8(i) * x^56 mod p(x) mod 2)
 */
static int initdone = 0;
static uint32_t * T[4];

/* Optimization: Precomputed value of T[0][0x80]. */
#define T_0_0x80 0x82f63b78

static void done(void);

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

/**
 * done(void):
 * Clean up tables.
 */
static void
done(void)
{

	free(T[3]);
	free(T[2]);
	free(T[1]);
	free(T[0]);
}

/**
 * init(void):
 * Initialize tables.
 */
static int
init(void)
{
	size_t i, j, k;
	uint32_t r;

	/* Are we already initialized? */
	if (initdone)
		return (0);

	/* Allocate space for arrays. */
	if ((T[0] = malloc(256 * sizeof(uint32_t))) == NULL)
		goto err0;
	if ((T[1] = malloc(256 * sizeof(uint32_t))) == NULL)
		goto err1;
	if ((T[2] = malloc(256 * sizeof(uint32_t))) == NULL)
		goto err2;
	if ((T[3] = malloc(256 * sizeof(uint32_t))) == NULL)
		goto err3;

	/* Fill in tables. */
	for (i = 0; i < 256; i++) {
		r = reverse(i);
		for (j = 0; j < 4; j++) {
			for (k = 0; k < 8; k++) {
				if (r & 0x80000000)
					r = (r << 1) ^ 0x1EDC6F41;
				else
					r = (r << 1);
			}
			T[j][i] = reverse(r);
		}
	}

	/*
	 * Clean up the tables when we exit, in order to simplify the task
	 * of tracking down memory leaks.
	 */
	if (atexit(done))
		goto err4;

	/* Make sure we optimized correctly. */
	assert(T[0][0x80] == T_0_0x80);

	/* Success! */
	initdone = 1;
	return (0);

err4:
	free(T[3]);
err3:
	free(T[2]);
err2:
	free(T[1]);
err1:
	free(T[0]);
err0:
	/* Failure! */
	return (-1);
}

/**
 * CRC32C_Init(ctx):
 * Initialize a CRC32C-computing context.  This function can only fail the
 * first time it is called.
 */
int
CRC32C_Init(CRC32C_CTX * ctx)
{

	/* Initialize tables. */
	if (initdone == 0) {
		if (init())
			return (-1);
	}

	/* Set state to the CRC of the implicit leading 1 bit. */
	ctx->state = T_0_0x80;

	/* Success! */
	return (0);
}

/**
 * CRC32C_Update(ctx, buf, len):
 * Feed ${len} bytes from the buffer ${buf} into the CRC32C being computed
 * via the context ${ctx}.
 */
void
CRC32C_Update(CRC32C_CTX * ctx, const uint8_t * buf, size_t len)
{

	/* Handle blocks of 4 bytes. */
	for (; len >= 4; len -= 4, buf += 4) {
		ctx->state =
		    T[0][((ctx->state >> 24) & 0xff) ^ buf[3]] ^
		    T[1][((ctx->state >> 16) & 0xff) ^ buf[2]] ^
		    T[2][((ctx->state >> 8)  & 0xff) ^ buf[1]] ^
		    T[3][((ctx->state)       & 0xff) ^ buf[0]];
	}

	/* Handle individual bytes. */
	for (; len > 0; len--, buf++) {
		ctx->state = (ctx->state >> 8) ^
		    T[0][((ctx->state) & 0xff) ^ buf[0]];
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

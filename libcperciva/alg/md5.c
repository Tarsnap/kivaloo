#include <stdint.h>
#include <string.h>

#include "insecure_memzero.h"
#include "sysendian.h"

#include "md5.h"

/*
 * Encode a length len/4 vector of (uint32_t) into a length len vector of
 * (uint8_t) in little-endian form.  Assumes len is a multiple of 4.
 */
static void
le32enc_vect(uint8_t * dst, const uint32_t * src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		le32enc(dst + i * 4, src[i]);
}

/*
 * Decode a little-endian length len vector of (uint8_t) into a length
 * len/4 vector of (uint32_t).  Assumes len is a multiple of 4.
 */
static void
le32dec_vect(uint32_t * dst, const uint8_t * src, size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++)
		dst[i] = le32dec(src + i * 4);
}

/* Elementary functions used by MD5 */
#define ROTL(x, n)	((x << n) | (x >> (32 - n)))
#define F(x, y, z)	((x & (y ^ z)) ^ z)
#define G(x, y, z)	((z & (x ^ y)) ^ y)
#define H(x, y, z)	(x ^ y ^ z)
#define I(x, y, z)	(((x) | (~z)) ^ y)

/* MD5 round functions */
#define FF(a, b, c, d, x, s)				\
	a = b + ROTL((a + F(b, c, d) + x), s)
#define GG(a, b, c, d, x, s)				\
	a = b + ROTL((a + G(b, c, d) + x), s)
#define HH(a, b, c, d, x, s)				\
	a = b + ROTL((a + H(b, c, d) + x), s)
#define II(a, b, c, d, x, s)				\
	a = b + ROTL((a + I(b, c, d) + x), s)

/* Adjusted round functions for rotating state */
#define FFr(S, W, i, s, T)				\
	FF(S[(64 - i) % 4], S[(65 - i) % 4],		\
	   S[(66 - i) % 4], S[(67 - i) % 4],		\
	    W[(i * 1 + 0) % 16] + T, s)
#define GGr(S, W, i, s, T)				\
	GG(S[(64 - i) % 4], S[(65 - i) % 4],		\
	   S[(66 - i) % 4], S[(67 - i) % 4],		\
	    W[(i * 5 + 1) % 16] + T, s)
#define HHr(S, W, i, s, T)				\
	HH(S[(64 - i) % 4], S[(65 - i) % 4],		\
	   S[(66 - i) % 4], S[(67 - i) % 4],		\
	    W[(i * 3 + 5) % 16] + T, s)
#define IIr(S, W, i, s, T)				\
	II(S[(64 - i) % 4], S[(65 - i) % 4],		\
	   S[(66 - i) % 4], S[(67 - i) % 4],		\
	    W[(i * 7 + 0) % 16] + T, s)

/*
 * MD5 block compression function.  The 128-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
static void
MD5_Transform(uint32_t * state, const uint8_t block[64])
{
	uint32_t W[16];
	uint32_t S[4];
	int i;

	/* 1. Prepare message schedule W. */
	le32dec_vect(W, block, 64);

	/* 2. Initialize working variables. */
	memcpy(S, state, 16);

	/* 3. Mix. */
	FFr(S, W,  0,  7, 0xd76aa478);
	FFr(S, W,  1, 12, 0xe8c7b756);
	FFr(S, W,  2, 17, 0x242070db);
	FFr(S, W,  3, 22, 0xc1bdceee);
	FFr(S, W,  4,  7, 0xf57c0faf);
	FFr(S, W,  5, 12, 0x4787c62a);
	FFr(S, W,  6, 17, 0xa8304613);
	FFr(S, W,  7, 22, 0xfd469501);
	FFr(S, W,  8,  7, 0x698098d8);
	FFr(S, W,  9, 12, 0x8b44f7af);
	FFr(S, W, 10, 17, 0xffff5bb1);
	FFr(S, W, 11, 22, 0x895cd7be);
	FFr(S, W, 12,  7, 0x6b901122);
	FFr(S, W, 13, 12, 0xfd987193);
	FFr(S, W, 14, 17, 0xa679438e);
	FFr(S, W, 15, 22, 0x49b40821);
	GGr(S, W, 16,  5, 0xf61e2562);
	GGr(S, W, 17,  9, 0xc040b340);
	GGr(S, W, 18, 14, 0x265e5a51);
	GGr(S, W, 19, 20, 0xe9b6c7aa);
	GGr(S, W, 20,  5, 0xd62f105d);
	GGr(S, W, 21,  9, 0x02441453);
	GGr(S, W, 22, 14, 0xd8a1e681);
	GGr(S, W, 23, 20, 0xe7d3fbc8);
	GGr(S, W, 24,  5, 0x21e1cde6);
	GGr(S, W, 25,  9, 0xc33707d6);
	GGr(S, W, 26, 14, 0xf4d50d87);
	GGr(S, W, 27, 20, 0x455a14ed);
	GGr(S, W, 28,  5, 0xa9e3e905);
	GGr(S, W, 29,  9, 0xfcefa3f8);
	GGr(S, W, 30, 14, 0x676f02d9);
	GGr(S, W, 31, 20, 0x8d2a4c8a);
	HHr(S, W, 32,  4, 0xfffa3942);
	HHr(S, W, 33, 11, 0x8771f681);
	HHr(S, W, 34, 16, 0x6d9d6122);
	HHr(S, W, 35, 23, 0xfde5380c);
	HHr(S, W, 36,  4, 0xa4beea44);
	HHr(S, W, 37, 11, 0x4bdecfa9);
	HHr(S, W, 38, 16, 0xf6bb4b60);
	HHr(S, W, 39, 23, 0xbebfbc70);
	HHr(S, W, 40,  4, 0x289b7ec6);
	HHr(S, W, 41, 11, 0xeaa127fa);
	HHr(S, W, 42, 16, 0xd4ef3085);
	HHr(S, W, 43, 23, 0x04881d05);
	HHr(S, W, 44,  4, 0xd9d4d039);
	HHr(S, W, 45, 11, 0xe6db99e5);
	HHr(S, W, 46, 16, 0x1fa27cf8);
	HHr(S, W, 47, 23, 0xc4ac5665);
	IIr(S, W, 48,  6, 0xf4292244);
	IIr(S, W, 49, 10, 0x432aff97);
	IIr(S, W, 50, 15, 0xab9423a7);
	IIr(S, W, 51, 21, 0xfc93a039);
	IIr(S, W, 52,  6, 0x655b59c3);
	IIr(S, W, 53, 10, 0x8f0ccc92);
	IIr(S, W, 54, 15, 0xffeff47d);
	IIr(S, W, 55, 21, 0x85845dd1);
	IIr(S, W, 56,  6, 0x6fa87e4f);
	IIr(S, W, 57, 10, 0xfe2ce6e0);
	IIr(S, W, 58, 15, 0xa3014314);
	IIr(S, W, 59, 21, 0x4e0811a1);
	IIr(S, W, 60,  6, 0xf7537e82);
	IIr(S, W, 61, 10, 0xbd3af235);
	IIr(S, W, 62, 15, 0x2ad7d2bb);
	IIr(S, W, 63, 21, 0xeb86d391);

	/* 4. Mix local working variables into global state. */
	for (i = 0; i < 4; i++)
		state[i] += S[i];

	/* Clean the stack. */
	insecure_memzero(W, 64);
	insecure_memzero(S, 16);
}

static uint8_t PAD[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Add padding and terminating bit-count. */
static void
MD5_Pad(MD5_CTX * ctx)
{
	uint8_t len[8];
	uint32_t r, plen;

	/*
	 * Convert length to a vector of bytes -- we do this now rather
	 * than later because the length will change after we pad.
	 */
	le32enc_vect(len, ctx->count, 8);

	/* Add 1--64 bytes so that the resulting length is 56 mod 64. */
	r = (ctx->count[0] >> 3) & 0x3f;
	plen = (r < 56) ? (56 - r) : (120 - r);
	MD5_Update(ctx, PAD, (size_t)plen);

	/* Add the terminating bit-count. */
	MD5_Update(ctx, len, 8);
}

/**
 * MD5_Init(ctx):
 * Initialize the MD5 context ${ctx}.
 */
void
MD5_Init(MD5_CTX * ctx)
{

	/* Zero bits processed so far. */
	ctx->count[0] = ctx->count[1] = 0;

	/* Magic initialization constants. */
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xEFCDAB89;
	ctx->state[2] = 0x98BADCFE;
	ctx->state[3] = 0x10325476;
}

/**
 * MD5_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the MD5 context ${ctx}.
 */
void
MD5_Update(MD5_CTX * ctx, const void * in, size_t len)
{
	uint32_t bitlen[2];
	uint32_t r;
	const uint8_t * src = in;

	/* Return immediately if we have nothing to do. */
	if (len == 0)
		return;

	/* Number of bytes left in the buffer from previous updates. */
	r = (ctx->count[0] >> 3) & 0x3f;

	/* Convert the length into a number of bits. */
	bitlen[0] = ((uint32_t)len) << 3;
	bitlen[1] = (uint32_t)(len >> 29);

	/* Update number of bits. */
	if ((ctx->count[0] += bitlen[0]) < bitlen[0])
		ctx->count[1]++;
	ctx->count[1] += bitlen[1];

	/* Handle the case where we don't need to perform any transforms. */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block. */
	memcpy(&ctx->buf[r], src, 64 - r);
	MD5_Transform(ctx->state, ctx->buf);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks. */
	while (len >= 64) {
		MD5_Transform(ctx->state, src);
		src += 64;
		len -= 64;
	}

	/* Copy left over data into buffer. */
	memcpy(ctx->buf, src, len);
}

/**
 * MD5_Final(digest, ctx):
 * Output the MD5 hash of the data input to the context ${ctx} into the
 * buffer ${digest}, and clear the context state.
 */
void
MD5_Final(uint8_t digest[16], MD5_CTX * ctx)
{

	/* Add padding. */
	MD5_Pad(ctx);

	/* Write the hash. */
	le32enc_vect(digest, ctx->state, 16);

	/* Clear the context state. */
	insecure_memzero(ctx, sizeof(MD5_CTX));
}

/**
 * MD5_Buf(in, len, digest):
 * Compute the MD5 hash of ${len} bytes from ${in} and write it to ${digest}.
 */
void
MD5_Buf(const void * in, size_t len, uint8_t digest[16])
{
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, in, len);
	MD5_Final(digest, &ctx);
}

/**
 * HMAC_MD5_Init(ctx, K, Klen):
 * Initialize the HMAC-MD5 context ${ctx} with ${Klen} bytes of key from ${K}.
 */
void
HMAC_MD5_Init(HMAC_MD5_CTX * ctx, const void * _k, size_t Klen)
{
	uint8_t pad[64];
	uint8_t khash[16];
	const uint8_t * K = _k;
	size_t i;

	/* If Klen > 64, the key is really MD5(K). */
	if (Klen > 64) {
		MD5_Init(&ctx->ictx);
		MD5_Update(&ctx->ictx, K, Klen);
		MD5_Final(khash, &ctx->ictx);
		K = khash;
		Klen = 16;
	}

	/* Inner MD5 operation is MD5(K xor [block of 0x36] || data). */
	MD5_Init(&ctx->ictx);
	memset(pad, 0x36, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	MD5_Update(&ctx->ictx, pad, 64);

	/* Outer MD5 operation is MD5(K xor [block of 0x5c] || hash). */
	MD5_Init(&ctx->octx);
	memset(pad, 0x5c, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
	MD5_Update(&ctx->octx, pad, 64);

	/* Clean the stack. */
	insecure_memzero(khash, 16);
	insecure_memzero(pad, 64);
}

/**
 * HMAC_MD5_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the HMAC-MD5 context ${ctx}.
 */
void
HMAC_MD5_Update(HMAC_MD5_CTX * ctx, const void * in, size_t len)
{

	/* Feed data to the inner MD5 operation. */
	MD5_Update(&ctx->ictx, in, len);
}

/**
 * HMAC_MD5_Final(digest, ctx):
 * Output the HMAC-MD5 of the data input to the context ${ctx} into the
 * buffer ${digest}.
 */
void
HMAC_MD5_Final(uint8_t digest[16], HMAC_MD5_CTX * ctx)
{
	uint8_t ihash[16];

	/* Finish the inner MD5 operation. */
	MD5_Final(ihash, &ctx->ictx);

	/* Feed the inner hash to the outer MD5 operation. */
	MD5_Update(&ctx->octx, ihash, 16);

	/* Finish the outer MD5 operation. */
	MD5_Final(digest, &ctx->octx);

	/* Clean the stack. */
	insecure_memzero(ihash, 16);
}

/**
 * HMAC_MD5_Buf(K, Klen, in, len, digest):
 * Compute the HMAC-MD5 of ${len} bytes from ${in} using the key ${K} of
 * length ${Klen}, and write the result to ${digest}.
 */
void
HMAC_MD5_Buf(const void * K, size_t Klen, const void * in, size_t len,
    uint8_t digest[16])
{
	HMAC_MD5_CTX ctx;

	HMAC_MD5_Init(&ctx, K, Klen);
	HMAC_MD5_Update(&ctx, in, len);
	HMAC_MD5_Final(digest, &ctx);
}

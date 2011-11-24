#ifndef _SHA1_H_
#define _SHA1_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Use #defines in order to avoid namespace collisions with anyone else's
 * SHA1 code (e.g., the code in OpenSSL).
 */
#define SHA1_Init libcperciva_SHA1_Init
#define SHA1_Update libcperciva_SHA1_Update
#define SHA1_Final libcperciva_SHA1_Final
#define SHA1_Buf libcperciva_SHA1_Buf
#define SHA1_CTX libcperciva_SHA1_CTX
#define HMAC_SHA1_Init libcperciva_HMAC_SHA1_Init
#define HMAC_SHA1_Update libcperciva_HMAC_SHA1_Update
#define HMAC_SHA1_Final libcperciva_HMAC_SHA1_Final
#define HMAC_SHA1_Buf libcperciva_HMAC_SHA1_Buf
#define HMAC_SHA1_CTX libcperciva_HMAC_SHA1_CTX

/* Context structure for SHA1 operations. */
typedef struct {
	uint32_t state[5];
	uint32_t count[2];
	uint8_t buf[64];
} SHA1_CTX;

/**
 * SHA1_Init(ctx):
 * Initialize the SHA1 context ${ctx}.
 */
void SHA1_Init(SHA1_CTX *);

/**
 * SHA1_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the SHA1 context ${ctx}.
 */
void SHA1_Update(SHA1_CTX *, const void *, size_t);

/**
 * SHA1_Final(digest, ctx):
 * Output the SHA1 hash of the data input to the context ${ctx} into the
 * buffer ${digest}.
 */
void SHA1_Final(uint8_t[20], SHA1_CTX *);

/**
 * SHA1_Buf(in, len, digest):
 * Compute the SHA1 hash of ${len} bytes from $in} and write it to ${digest}.
 */
void SHA1_Buf(const void *, size_t, uint8_t[20]);

/* Context structure for HMAC-SHA1 operations. */
typedef struct {
	SHA1_CTX ictx;
	SHA1_CTX octx;
} HMAC_SHA1_CTX;

/**
 * HMAC_SHA1_Init(ctx, K, Klen):
 * Initialize the HMAC-SHA1 context ${ctx} with ${Klen} bytes of key from ${K}.
 */
void HMAC_SHA1_Init(HMAC_SHA1_CTX *, const void *, size_t);

/**
 * HMAC_SHA1_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the HMAC-SHA1 context ${ctx}.
 */
void HMAC_SHA1_Update(HMAC_SHA1_CTX *, const void *, size_t);

/**
 * HMAC_SHA1_Final(digest, ctx):
 * Output the HMAC-SHA1 of the data input to the context ${ctx} into the
 * buffer ${digest}.
 */
void HMAC_SHA1_Final(uint8_t[20], HMAC_SHA1_CTX *);

/**
 * HMAC_SHA1_Buf(K, Klen, in, len, digest):
 * Compute the HMAC-SHA1 of ${len} bytes from ${in} using the key ${K} of
 * length ${Klen}, and write the result to ${digest}.
 */
void HMAC_SHA1_Buf(const void *, size_t, const void *, size_t, uint8_t[20]);

#endif /* !_SHA1_H_ */

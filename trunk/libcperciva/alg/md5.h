#ifndef _MD5_H_
#define _MD5_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Use #defines in order to avoid namespace collisions with anyone else's
 * MD5 code (e.g., the code in OpenSSL).
 */
#define MD5_Init libcperciva_MD5_Init
#define MD5_Update libcperciva_MD5_Update
#define MD5_Final libcperciva_MD5_Final
#define MD5_Buf libcperciva_MD5_Buf
#define MD5_CTX libcperciva_MD5_CTX
#define HMAC_MD5_Init libcperciva_HMAC_MD5_Init
#define HMAC_MD5_Update libcperciva_HMAC_MD5_Update
#define HMAC_MD5_Final libcperciva_HMAC_MD5_Final
#define HMAC_MD5_Buf libcperciva_HMAC_MD5_Buf
#define HMAC_MD5_CTX libcperciva_HMAC_MD5_CTX

/* Context structure for MD5 operations. */
typedef struct {
	uint32_t state[4];
	uint32_t count[2];
	uint8_t buf[64];
} MD5_CTX;

/**
 * MD5_Init(ctx):
 * Initialize the MD5 context ${ctx}.
 */
void MD5_Init(MD5_CTX *);

/**
 * MD5_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the MD5 context ${ctx}.
 */
void MD5_Update(MD5_CTX *, const void *, size_t);

/**
 * MD5_Final(digest, ctx):
 * Output the MD5 hash of the data input to the context ${ctx} into the
 * buffer ${digest}.
 */
void MD5_Final(uint8_t[16], MD5_CTX *);

/**
 * MD5_Buf(in, len, digest):
 * Compute the MD5 hash of ${len} bytes from $in} and write it to ${digest}.
 */
void MD5_Buf(const void *, size_t, uint8_t[16]);

/* Context structure for HMAC-MD5 operations. */
typedef struct {
	MD5_CTX ictx;
	MD5_CTX octx;
} HMAC_MD5_CTX;

/**
 * HMAC_MD5_Init(ctx, K, Klen):
 * Initialize the HMAC-MD5 context ${ctx} with ${Klen} bytes of key from ${K}.
 */
void HMAC_MD5_Init(HMAC_MD5_CTX *, const void *, size_t);

/**
 * HMAC_MD5_Update(ctx, in, len):
 * Input ${len} bytes from ${in} into the HMAC-MD5 context ${ctx}.
 */
void HMAC_MD5_Update(HMAC_MD5_CTX *, const void *, size_t);

/**
 * HMAC_MD5_Final(digest, ctx):
 * Output the HMAC-MD5 of the data input to the context ${ctx} into the
 * buffer ${digest}.
 */
void HMAC_MD5_Final(uint8_t[16], HMAC_MD5_CTX *);

/**
 * HMAC_MD5_Buf(K, Klen, in, len, digest):
 * Compute the HMAC-MD5 of ${len} bytes from ${in} using the key ${K} of
 * length ${Klen}, and write the result to ${digest}.
 */
void HMAC_MD5_Buf(const void *, size_t, const void *, size_t, uint8_t[16]);

#endif /* !_MD5_H_ */

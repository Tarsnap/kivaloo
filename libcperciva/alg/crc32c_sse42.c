#include "cpusupport.h"
#ifdef CPUSUPPORT_X86_SSE42
/**
 * CPUSUPPORT CFLAGS: X86_SSE42 X86_SSE42_64
 *
 * The latter value will be unset on 32-bit systems, but that's ok.
 * I'm not currently aware of any compiler/platform which requires additional
 * flags for X86_SSE42_64 which are not also needed for X86_SSE42, but it's
 * safest to include both sets of flags.
 */

#include <assert.h>
#include <smmintrin.h>

#include "crc32c_sse42.h"

/**
 * CRC32C_Update_SSE42(state, buf, len):
 * Feed ${len} bytes from the buffer ${buf} into the CRC32C whose state is
 * ${state}.  This implementation uses x86 SSE4.2 instructions, and should only
 * be used if CPUSUPPORT_X86_SSE42 is defined and cpusupport_x86_sse42()
 * returns nonzero.  If CPUSUPPORT_X86_SSE42_64 is also defined, 64-bit
 * instructions may be used.  ${len} must be greater than, or equal to, 8.
 */
uint32_t
CRC32C_Update_SSE42(uint32_t state, const uint8_t * buf, size_t len)
{
	size_t pre_block;
	size_t remaining_bytes;
	size_t in_block;
	size_t i = 0;

	/* Sanity test. */
	assert(len >= 8);

	/*
	 * Calculate how many bytes are before an aligned block.  Assume that
	 * the (uintptr_t) value indicates the memory layout.
	 */
	pre_block = (8 - (uintptr_t)buf) & 7;

	/* Calculate how many bytes are in aligned blocks. */
	remaining_bytes = len - pre_block;
	in_block = remaining_bytes - (remaining_bytes % 8);

	/* Process bytes before the alignment. */
	for (; i < pre_block; i++)
		state = _mm_crc32_u8(state, buf[i]);

	/* If we would process a block, ensure that it's aligned. */
	assert(!((i < in_block) && !((((uintptr_t)&buf[i]) & 7) == 0)));

	/*
	 * Process blocks of 8.  It's ok if (i % 8 != 0), because we're
	 * checking that (i < in_block), not (i != in_block).  For example,
	 * if we start with i = 4 and want 1 block, we'll end up at i == 12.
	 */
	for (; i < in_block; i += 8) {
#ifdef CPUSUPPORT_X86_SSE42_64
		state = (uint32_t)_mm_crc32_u64(state,
		    *(const uint64_t *)&buf[i]);
#else
		/* Only process 32 bits at once. */
		state = (uint32_t)_mm_crc32_u32(state,
		    *(const uint32_t *)&buf[i]);
		state = (uint32_t)_mm_crc32_u32(state,
		    *(const uint32_t *)&buf[i + 4]);
#endif
	}

	/* Ensure that we don't have too many bytes remaining. */
	assert((len - i) < 8);

	/* Process any remaining bytes. */
	for (; i < len; i++)
		state = _mm_crc32_u8(state, buf[i]);

	return (state);
}

#endif /* CPUSUPPORT_X86_SSE42 */

#include "cpusupport.h"
#ifdef CPUSUPPORT_ARM_CRC32_64
/**
 * CPUSUPPORT CFLAGS: ARM_CRC32_64
 */

#ifdef __ARM_ACLE
#include <arm_acle.h>
#endif

#include <assert.h>

#include "crc32c_arm.h"

/**
 * CRC32C_Update_ARM(state, buf, len):
 * Feed ${len} bytes from the buffer ${buf} into the CRC32C whose state is
 * ${state}.  This implementation uses ARM CRC32 instructions, and should only
 * be used if CPUSUPPORT_ARM_CRC32_64 is defined and cpusupport_arm_crc32()
 * returns nonzero.  ${len} must be greater than, or equal to, 8.
 */
uint32_t
CRC32C_Update_ARM(uint32_t state, const uint8_t * buf, size_t len)
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
		state = __crc32cb(state, buf[i]);

	/* If we would process a block, ensure that it's aligned. */
	assert(!((i < in_block) && !((((uintptr_t)&buf[i]) & 7) == 0)));

	/*
	 * Process blocks of 8.  It's ok if (i % 8 != 0), because we're
	 * checking that (i < in_block), not (i != in_block).  For example,
	 * if we start with i = 4 and want 1 block, we'll end up at i == 12.
	 */
	for (; i < in_block; i += 8) {
		state = __crc32cd(state,
		    *(const uint64_t *)(&buf[i]));
	}

	/* Ensure that we don't have too many bytes remaining. */
	assert((len - i) < 8);

	/* Process any remaining bytes. */
	for (; i < len; i++)
		state = __crc32cb(state, buf[i]);

	return (state);
}

#endif /* CPUSUPPORT_ARM_CRC32_64 */

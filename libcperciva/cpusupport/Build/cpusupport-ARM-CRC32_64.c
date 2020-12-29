#ifdef __ARM_ACLE
#include <arm_acle.h>
#endif

#ifndef __ARM_FEATURE_CRC32
#error "ARM_FEATURE_CRC32 not supported"
#endif

int
main(void)
{
	uint32_t state = 0;
	uint8_t x = 0;
	uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	/* Test 8-bit and 64-bit. */
	state = __crc32b(state, x);
	state = __crc32cd(state, *(const uint64_t *)&buf[0]);

	return (0);
}

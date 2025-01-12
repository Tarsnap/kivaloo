#include <smmintrin.h>

#include <stdint.h>

int
main(void)
{
	unsigned int state = 0;
	uint8_t x = 0;
	uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	/* Check the 8-bit CRC32 instruction. */
	state = _mm_crc32_u8(state, x);

	/* Check the 32-bit CRC32 instruction. */
	state = (uint32_t)_mm_crc32_u64(state, *(const uint64_t *)&buf[0]);
	return ((int)state);
}

#include <smmintrin.h>

#include <stdint.h>

int
main(void)
{
	unsigned int state = 0;
	unsigned char x = 0;
	uint8_t buf[4] = {0, 0, 0, 0};

	/* Test both the 8-bit and 32-bit data versions. */
	state = _mm_crc32_u8(state, x);

	state = (uint32_t)_mm_crc32_u32(state, *(const uint32_t *)&buf[0]);
	return ((int)state);
}

#ifndef CRC32C_ARM_H_
#define CRC32C_ARM_H_

#include <stddef.h>
#include <stdint.h>

/**
 * CRC32C_Update_ARM(state, buf, len):
 * Feed ${len} bytes from the buffer ${buf} into the CRC32C whose state is
 * ${state}.  This implementation uses ARM CRC32 instructions, and should only
 * be used if CPUSUPPORT_ARM_CRC32_64 is defined and cpusupport_arm_crc32()
 * returns nonzero.  ${len} must be greater than, or equal to, 8.
 */
uint32_t CRC32C_Update_ARM(uint32_t, const uint8_t *, size_t);

#endif /* !CRC32C_ARM_H_ */

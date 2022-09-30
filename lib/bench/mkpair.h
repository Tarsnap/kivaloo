#ifndef MKPAIR_H_
#define MKPAIR_H_

#include <stdint.h>

/**
 * mkkey(X, Y, buf):
 * Write the 40-byte key
 * offset  length
 * ------  ------
 *    0       8    64-bit big-endian X
 *    8      32    sha256(\000.(64-bit big-endian X).(64-bit big-endian Y))
 * into ${buf}.
 */
void mkkey(uint64_t, uint64_t, uint8_t *);

/**
 * mkval(X, Y, buf):
 * Write the 40-byte value
 * offset  length
 * ------  ------
 *    0       8    64-bit big-endian X * 2^16 + Y
 *    8      32    sha256(\001.(64-bit big-endian X).(64-bit big-endian Y))
 * into ${buf}.
 */
void mkval(uint64_t, uint64_t, uint8_t *);

#endif /* !MKPAIR_H_ */

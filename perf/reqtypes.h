#ifndef REQTYPES_H_
#define REQTYPES_H_

#include <stdint.h>

/**
 * reqtypes_lookup(id):
 * Return the symbolic name associated with a kivaloo request type.
 */
const char * reqtypes_lookup(uint32_t);

#endif /* !REQTYPES_H_ */

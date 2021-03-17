#ifndef _REQTYPES_H_
#define _REQTYPES_H_

#include <stdint.h>

/**
 * reqtypes_lookup(id):
 * Return the symbolic name associated with a kivaloo request type.
 */
const char * reqtypes_lookup(uint32_t);

#endif /* !_REQTYPES_H_ */

#ifndef _OBJMAP_H_
#define _OBJMAP_H_

#include <stdint.h>

/**
 * objmap(N):
 * Convert the S3 object number ${N} into an object name.  Return a statically
 * allocated string which is valid until the next call to objmap.
 */
const char * objmap(uint64_t);

#endif /* !_OBJMAP_H_ */

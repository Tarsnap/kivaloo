#ifndef OBJMAP_H_
#define OBJMAP_H_

#include <stdint.h>

/**
 * objmap(N):
 * Convert the S3 object number ${N} into an object name.  Return a statically
 * allocated string which is valid until the next call to objmap().
 */
const char * objmap(uint64_t);

#endif /* !OBJMAP_H_ */

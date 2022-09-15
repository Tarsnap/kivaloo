#ifndef OBJMAP_H_
#define OBJMAP_H_

#include <stdint.h>

/**
 * objmap(N):
 * Convert the page number ${N} into a (string) key.  Return a statically
 * allocated string which is valid until the next call to objmap().
 */
const char * objmap(uint64_t);

#endif /* !OBJMAP_H_ */

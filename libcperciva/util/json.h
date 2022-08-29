#ifndef JSON_H_
#define JSON_H_

#include <stdint.h>

/**
 * json_find(buf, end, s):
 * If there is a valid JSON object which starts at ${buf} and ends before or
 * at ${end} and said object contains a name/value pair with name ${s},
 * return a pointer to the associated value.  Otherwise, return ${end}.
 */
const uint8_t * json_find(const uint8_t *, const uint8_t *, const char *);

#endif /* !JSON_H_ */

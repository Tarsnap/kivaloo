#ifndef _STORAGE_FINDFILES_H_
#define _STORAGE_FINDFILES_H_

#include <sys/types.h>

#include <stdint.h>

struct storage_file {
	uint64_t fileno;	/* Hex digits in "blks_<16 hex digits>". */
	off_t len;		/* Length of the file, in bytes. */
};

/**
 * storage_findfiles(path):
 * Look for files named "blks_<16 hex digits>" in the directory ${path}.
 * Return an elastic queue of struct storage_file, in order of increasing
 * fileno.
 */
struct elasticqueue * storage_findfiles(const char *);

#endif /* !_STORAGE_FINDFILES_H_ */

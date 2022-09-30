#ifndef DISK_H_
#define DISK_H_

#include <stdint.h>
#include <unistd.h>

/**
 * disk_syncdir(path):
 * Make sure the directory ${path} is synced to disk.  On some systems, it is
 * necessary to call this after creat/write/fsync on a file in order to make
 * sure that the file will be present after a crash; on many systems it is
 * necessary to call this after unlink on a file in order to ensure that the
 * file will not be present after a crash.
 */
int disk_syncdir(const char *);

/**
 * disk_read(path, offset, nbytes, buf):
 * Read ${nbytes} bytes from position ${offset} in file ${path} into the
 * buffer ${buf}.  Treat EOF as an error.  If the file ${path} does not
 * exist, fail and return with errno set to ENOENT.
 */
int disk_read(const char *, off_t, size_t, uint8_t *);

/**
 * disk_write(path, creat, nbytes, buf, nosync):
 * Append ${nbytes} from ${buf} to the end of the file ${path} and fsync.  If
 * ${creat} is non-zero, create the file (which should not exist yet) first
 * with 0600 permissions.  If ${nosync} is non-zero, skip the fsync.
 */
int disk_write(const char *, int, size_t, const uint8_t *, int);

#endif /* !DISK_H_ */

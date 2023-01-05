#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>

#include "noeintr.h"
#include "warnp.h"

#include "disk.h"

/* Use O_BINARY when opening files in order to keep windows happy. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/**
 * disk_syncdir(path):
 * Make sure the directory ${path} is synced to disk.  On some systems, it is
 * necessary to call this after creat/write/fsync on a file in order to make
 * sure that the file will be present after a crash; on many systems it is
 * necessary to call this after unlink on a file in order to ensure that the
 * file will not be present after a crash.
 */
int
disk_syncdir(const char * path)
{
	int fd;

	while ((fd = open(path, O_RDONLY)) == -1) {
		if (errno != EINTR) {
			warnp("open(%s)", path);
			goto err0;
		}
	}
	while (fsync(fd)) {
		if (errno != EINTR) {
			warnp("fsync(%s)", path);
			goto err1;
		}
	}
	while (close(fd)) {
		if (errno != EINTR) {
			warnp("close(%s)", path);
			goto err0;
		}
	}

	/* Success! */
	return (0);

err1:
	if (close(fd))
		warnp("close");
err0:
	/* Failure! */
	return (-1);
}

/**
 * disk_read(path, offset, nbytes, buf):
 * Read ${nbytes} bytes from position ${offset} in file ${path} into the
 * buffer ${buf}.  Treat EOF as an error.  If the file ${path} does not
 * exist, fail and return with errno set to ENOENT.
 */
int
disk_read(const char * path, off_t offset, size_t nbytes, uint8_t * buf)
{
	int fd;
	size_t bufpos;
	ssize_t lenread;

	/*
	 * Attempt to open the file.  Pass an errno value of ENOENT back
	 * without printing a warning, since it might be a non-error.
	 */
	while ((fd = open(path, O_RDONLY | O_BINARY)) == -1) {
		/* Try again on EINTR. */
		if (errno == EINTR)
			continue;

		/* Fail without printing a warning on ENOENT. */
		if (errno == ENOENT)
			goto err0;

		/* Print a warning and fail for anything else. */
		warnp("open(%s)", path);
		goto err0;
	}

	/* Seek to the appropriate position. */
	if (lseek(fd, offset, SEEK_SET) == -1) {
		warnp("lseek(%s, %" PRIu64 ")", path, (uint64_t)(offset));
		goto err1;
	}

	/* Read into the buffer. */
	for (bufpos = 0; bufpos < nbytes; bufpos += (size_t)lenread) {
		/* Read some bytes. */
		lenread = read(fd, &buf[bufpos], nbytes - bufpos);

		/* EOF? */
		if (lenread == 0) {
			warn0("Unexpected EOF reading file: %s", path);
			goto err1;
		}

		/* EINTR is harmless. */
		if ((lenread == -1) && (errno == EINTR))
			lenread = 0;

		/* Print a warning and fail on other errors. */
		if (lenread == -1) {
			warnp("Error reading file: %s", path);
			goto err1;
		}
	}

	/* Close the file. */
	while (close(fd)) {
		if (errno != EINTR) {
			warnp("close(%s)", path);
			goto err0;
		}
	}

	/* Success! */
	return (0);

err1:
	if (close(fd))
		warnp("close");
err0:
	/* Failure! */
	return (-1);
}

/**
 * disk_write(path, creat, nbytes, buf, nosync):
 * Append ${nbytes} from ${buf} to the end of the file ${path} and fsync.  If
 * ${creat} is non-zero, create the file (which should not exist yet) first
 * with 0600 permissions.  If ${nosync} is non-zero, skip the fsync.
 */
int
disk_write(const char * path, int create, size_t nbytes, const uint8_t * buf,
    int nosync)
{
	int fd;

	/* Open or create the file, depending on ${creat}. */
	do {
		/* Attempt to open/create. */
		if (create) {
			fd = open(path, O_WRONLY | O_BINARY | O_APPEND |
			    O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
		} else {
			fd = open(path, O_WRONLY | O_BINARY | O_APPEND);
		}

		/* If we hit EINTR, try again. */
	} while ((fd == -1) && (errno == EINTR));

	/* If we failed to open the file, error out. */
	if (fd == -1) {
		warnp("open(%s)", path);
		goto err0;
	}

	/* Write from the buffer. */
	if (noeintr_write(fd, buf, nbytes) != (ssize_t)nbytes)
		goto err1;

	/* Ask to have the write flushed to disk. */
	if (nosync == 0) {
		while (fsync(fd)) {
			if (errno != EINTR) {
				warnp("fsync(%s)", path);
				goto err1;
			}
		}
	}

	/* Close the file. */
	while (close(fd)) {
		if (errno != EINTR) {
			warnp("close(%s)", path);
			goto err0;
		}
	}

	/* Success! */
	return (0);

err1:
	if (close(fd))
		warnp("close");
err0:
	/* Failure! */
	return (-1);
}

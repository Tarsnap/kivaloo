#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "events.h"
#include "noeintr.h"
#include "warnp.h"

#include "logging.h"

/* Log file structure. */
struct logging_file {
	int fd;			/* File descriptor open to file. */
	char * path;		/* Copy of path string. */
	void * timer_cookie;	/* Cookie for has-file-moved timer. */
};

/* Open the log file and EOL-terminate if necessary. */
static int
doopen(const char * path)
{
	struct stat sb;
	int fd;
	char lastbyte;

	/* Attempt to open the file. */
	if ((fd = open(path, O_RDWR | O_APPEND | O_CREAT,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
		warnp("Cannot open log file: %s", path);
		goto err0;
	}

	/* Make sure the log file we opened is a regular file. */
	if (fstat(fd, &sb)) {
		warnp("fstat(%s)", path);
		goto err1;
	}
	if (! S_ISREG(sb.st_mode)) {
		warnp("Log file is not a regular file: %s", path);
		goto err1;
	}

	/* If the file is empty, it doesn't need EOL-termination. */
	if (sb.st_size == 0)
		goto done;

	/* Check if the last byte of the file is '\n'. */
	if (lseek(fd, -1, SEEK_END) == -1) {
		warnp("lseek(%s, -1, SEEK_END)", path);
		goto err1;
	}
	if (read(fd, &lastbyte, 1) != 1) {
		warnp("read(%s)", path);
		goto err1;
	}
	if (lastbyte == '\n')
		goto done;

	/* EOL-terminate the file. */
	lastbyte = '\n';
	if (noeintr_write(fd, &lastbyte, 1) != 1) {
		warnp("Cannot EOL-terminate log file: %s", path);
		goto err1;
	}

done:
	/* Success! */
	return (fd);

err1:
	close(fd);
err0:
	/* Failure! */
	return (-1);
}

/* Check if we need to close and re-open the log file. */
static int
callback_timer(void * cookie)
{
	struct stat sb_fd;
	struct stat sb_path;
	struct logging_file * F = cookie;

	/* Timer callback is no longer pending. */
	F->timer_cookie = NULL;

	/* Stat the file and the path. */
	if (fstat(F->fd, &sb_fd)) {
		warnp("fstat(%s)", F->path);
		goto err0;
	}
	if (stat(F->path, &sb_path)) {
		/* If the path doesn't exist, we need to reopen the file. */
		if (errno == ENOENT)
			goto reopen;

		/* Any other error is bad. */
		warnp("stat(%s)", F->path);
		goto err0;
	}

	/* If the path still points at the same file, do nothing. */
	if (S_ISREG(sb_fd.st_mode) && S_ISREG(sb_path.st_mode) &&
	    (sb_fd.st_dev == sb_path.st_dev) &&
	    (sb_fd.st_ino == sb_path.st_ino))
		goto done;

reopen:
	/* We need to close and re-open the log file. */
	close(F->fd);
	if ((F->fd = doopen(F->path)) == -1)
		goto err0;

done:
	/* Reset the callback. */
	if ((F->timer_cookie =
	    events_timer_register_double(callback_timer, F, 1.0)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * logging_open(path):
 * Open the file ${path}, creating it if necessary.  If the file has non-zero
 * length and the final character is not '\n', append a '\n' character.  Once
 * per second, check if ${path} still points at the open file; if not, close
 * it and re-open ${path}.
 *
 * Note that applications using this function should not use chroot and should
 * only use chdir if ${path} is an absolute path; otherwise the log file may
 * be re-created in the wrong place.
 */
struct logging_file *
logging_open(const char * path)
{
	struct logging_file * F;

	/* Allocate a log file structure. */
	if ((F = malloc(sizeof(struct logging_file))) == NULL)
		goto err0;

	/* Duplicate the provided path. */
	if ((F->path = strdup(path)) == NULL)
		goto err1;

	/* Open the file. */
	if ((F->fd = doopen(F->path)) == -1)
		goto err2;

	/* Start the has-file-moved timer. */
	if ((F->timer_cookie =
	    events_timer_register_double(callback_timer, F, 1.0)) == NULL)
		goto err3;

	/* Success! */
	return (F);

err3:
	close(F->fd);
err2:
	free(F->path);
err1:
	free(F);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * logging_printf(F, format, ...):
 * Write <datetime><printf-formatted-string><\n> to the log file for which ${F}
 * was returned by logging_open, where <printf-formatted-string> is formatted
 * as per the printf functions using ${format} and any additional arguments,
 * and <datetime> is of the form "YYYY-MM-DD hh:mm:ss".  Note that there is
 * no separator after <datetime>; that should be included in the format string.
 * Return the number of characters written (including the datetime and EOL), or
 * -1 on error.
 */
ssize_t
logging_printf(struct logging_file * F, const char * format, ...)
{
	va_list ap;
	int len;
	size_t buflen;
	char * str;
	time_t now;

	/* Figure out how long the line we're writing is. */
	va_start(ap, format);
	len = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	/* Did we fail? */
	if (len < 0)
		goto err0;
	buflen = (size_t)(len) + 21; /* "YYYY-MM-DD hh:mm:ss\n\0" */

	/* Allocate memory. */
	if ((str = malloc(buflen)) == NULL)
		goto err0;

	/* Write date and time to start of buffer. */
	if (time(&now) == (time_t)(-1)) {
		warnp("time");
		goto err1;
	}
	if (strftime(str, 20, "%Y-%m-%d %H:%M:%S", gmtime(&now)) != 19) {
		warnp("strftime");
		goto err1;
	}

	/* Append the log message. */
	va_start(ap, format);
	len = vsnprintf(&str[19], buflen - 19, format, ap);
	va_end(ap);

	/* Did we fail? */
	if (len < 0)
		goto err1;

	/* Make sure vsnprintf is deterministic. */
	assert(buflen == (size_t)(len) + 21);

	/* Add EOL and re-NUL-terminate. */
	str[19 + len] = '\n';
	str[20 + len] = '\0';

	/* Write the final string (sans terminating NUL) to the file. */
	if (noeintr_write(F->fd, str, buflen - 1) != (ssize_t)(buflen - 1)) {
		warnp("Cannot write to log file: %s", F->path);
		goto err1;
	}

	/* Free our string buffer. */
	free(str);

	/* Success! */
	return (0);

err1:
	free(str);
err0:
	/* Failure! */
	return (-1);
}

/**
 * logging_close(F):
 * Close the log file for which ${F} was returned by logging_open.
 */
void
logging_close(struct logging_file * F)
{

	/* Stop the has-file-moved timer if it's running. */
	if (F->timer_cookie != NULL)
		events_timer_cancel(F->timer_cookie);

	/* Close the log file if we have it open. */
	if (F->fd != -1)
		close(F->fd);

	/* Free the duplicated path string. */
	free(F->path);

	/* Free the structure. */
	free(F);
}

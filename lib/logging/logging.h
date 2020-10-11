#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <unistd.h>

/**
 * Rotatable log file writer.  If the log file /path/to/foo.log is written by
 * a single process using these functions, the operations
 * # mv /path/to/foo.log /path/to/foo.log.old
 * # while ! [ -f /path/to/foo.log ]; do sleep 1; done
 * will safely rotate the log file and wait until writes to it have ceased.
 */

/* Opaque log file structure. */
struct logging_file;

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
struct logging_file * logging_open(const char *);

/**
 * logging_printf(F, format, ...):
 * Write <datetime><printf-formatted-string><\n> to the log file for which ${F}
 * was returned by logging_open(), where <printf-formatted-string> is formatted
 * as per the printf functions using ${format} and any additional arguments,
 * and <datetime> is of the form "YYYY-MM-DD hh:mm:ss".  Note that there is
 * no separator after <datetime>; that should be included in the format string.
 * Return the number of characters written (including the datetime and EOL), or
 * -1 on error.
 */
ssize_t logging_printf(struct logging_file *, const char *, ...);

/**
 * logging_close(F):
 * Close the log file for which ${F} was returned by logging_open().
 */
void logging_close(struct logging_file *);

#endif /* !_LOGGING_H_ */

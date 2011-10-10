#ifndef _NETBUF_H_
#define _NETBUF_H_

#include <stdint.h>

/**
 * netbuf_read_init(s):
 * Create and return a buffered reader attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to read from said
 * socket except via the returned reader.
 */
struct netbuf_read * netbuf_read_init(int);

/**
 * netbuf_read_peek(R, data, datalen):
 * Set ${data} to point to the currently buffered data in the reader ${R}; set
 * ${datalen} to the number of bytes buffered.
 */
void netbuf_read_peek(struct netbuf_read *, uint8_t **, size_t *);

/**
 * netbuf_read_wait(R, len, callback, cookie):
 * Wait until ${R} has ${len} or more bytes of data buffered or an error
 * occurs; then invoke ${callback}(${cookie}, status) with status set to 0
 * if the data is available, and set to 1 on error.
 */
int netbuf_read_wait(struct netbuf_read *, size_t,
    int (*)(void *, int), void *);

/**
 * netbuf_read_wait_cancel(R):
 * Cancel the in-progress wait on the reader ${R}.  Do not invoke the callback
 * associated with the wait.
 */
void netbuf_read_wait_cancel(struct netbuf_read *);

/**
 * netbuf_read_consume(R, len):
 * Advance the reader pointer for the reader ${R} by ${len} bytes.
 */
void netbuf_read_consume(struct netbuf_read *, size_t);

/**
 * netbuf_read_read(R, buf, buflen, callback, cookie):
 * Read ${buflen} bytes into the buffer ${buf} via the buffered reader ${R}.
 * Invoke ${callback}(${cookie}, status) when done, with status set to 0 on
 * success, and set to 1 on failure.
 */
int netbuf_read_read(struct netbuf_read *, uint8_t *, size_t,
    int (*)(void *, int), void *);

/**
 * netbuf_read_cancel(R):
 * Cancel the in-progress read on the reader ${R}.  Do not invoke the
 * callback associated with the read.
 */
void netbuf_read_cancel(struct netbuf_read *);

/**
 * netbuf_read_free(R):
 * Free the reader ${R}.  Note that an indeterminate amount of data may have
 * been buffered and will be lost.
 */
void netbuf_read_free(struct netbuf_read *);

/**
 * netbuf_write_init(s, fail_callback, fail_cookie):
 * Create and return a buffered writer attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to write to said
 * socket except via the returned writer until netbuf_write_free is called.
 * to destroy the writer.  If a write fails, ${fail_callback} will be invoked
 * with the parameter ${fail_cookie}.
 */
struct netbuf_write * netbuf_write_init(int, int (*)(void *), void *);

/**
 * netbuf_write_reserve(W, len):
 * Reserve ${len} bytes of space in the buffered writer ${W} and return a
 * pointer to the buffer.  This operation must be followed by a call to
 * netbuf_write_consume before the next call to _reserve or _write and before
 * a callback could be made into netbuf_write (i.e., before control returns
 * to the event loop).
 */
uint8_t * netbuf_write_reserve(struct netbuf_write *, size_t);

/**
 * netbuf_write_consume(W, len):
 * Consume a reservation previously made by netbuf_write_reserve; the value
 * ${len} must be <= the value passed to netbuf_write_reserve.
 */
int netbuf_write_consume(struct netbuf_write *, size_t);

/**
 * netbuf_write_write(W, buf, buflen):
 * Write ${buflen} bytes from the buffer ${buf} via the buffered writer ${W}.
 */
int netbuf_write_write(struct netbuf_write *, const uint8_t *, size_t);

/**
 * netbuf_write_free(W):
 * Free the writer ${W}.
 */
void netbuf_write_free(struct netbuf_write *);

#endif /* !_NETBUF_H_ */

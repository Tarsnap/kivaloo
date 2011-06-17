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
 * netbuf_write_init(s):
 * Create and return a buffered writer attached to socket ${s}.  The caller
 * is responsible for ensuring that no attempts are made to write to said
 * socket except via the returned writer until netbuf_write_destroy is called
 * to destroy the writer.
 */
struct netbuf_write * netbuf_write_init(int);

/**
 * netbuf_write_write(W, buf, buflen, callback, cookie):
 * Write ${buflen} bytes from the buffer ${buf} via the buffered writer ${W}.
 * Invoke ${callback}(${cookie}, status) when done, with status set to 0 on
 * success, and set to 1 on failure.  A call to netbuf_write_destroy can be
 * made from within the callback, but not a call to netbuf_write_free.
 */
int netbuf_write_write(struct netbuf_write *, const uint8_t *, size_t,
    int (*)(void *, int), void *);

/**
 * netbuf_write_destroy(W):
 * Destroy the writer ${W}.  The write-completion callbacks will be queued to
 * be performed as failures after netbuf_write_destroy returns.  On error
 * return, the queue will be destroyed but some callbacks may be lost.
 */
int netbuf_write_destroy(struct netbuf_write *);

/**
 * netbuf_write_free(W):
 * Free the writer ${W}.  The writer must have been previously destroyed by a
 * call to netbuf_write_free.
 */
void netbuf_write_free(struct netbuf_write *);

#endif /* !_NETBUF_H_ */

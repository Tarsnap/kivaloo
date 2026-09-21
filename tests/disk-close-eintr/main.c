/*
 * Regression for retrying close(2) after EINTR.
 *
 * The close fixture models platforms where the descriptor is already released
 * when EINTR is reported.  A second close would therefore target a reused fd.
 */
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_open(const char *, int, ...);
static int test_close(int);
static int test_fsync(int);
static off_t test_lseek(int, off_t, int);
static ssize_t test_read(int, void *, size_t);
static ssize_t test_noeintr_write(int, const void *, size_t);

#define open test_open
#define close test_close
#define fsync test_fsync
#define lseek test_lseek
#define read test_read
#define noeintr_write test_noeintr_write
#include "../../lbs/disk.c"
#undef noeintr_write
#undef read
#undef lseek
#undef fsync
#undef close
#undef open

static int close_calls;
static const int fixture_fd = 17;

static int
test_open(const char * path, int flags, ...)
{
	(void)path;
	(void)flags;
	return (fixture_fd);
}

static int
test_close(int fd)
{
	assert(fd == fixture_fd);
	close_calls++;

	/*
	 * The first call reports EINTR after releasing fd.  Returning success on
	 * a second call lets the old retry loop terminate, but that second call
	 * represents closing an unrelated descriptor which reused this number.
	 */
	if (close_calls == 1) {
		errno = EINTR;
		return (-1);
	}
	return (0);
}

static int
test_fsync(int fd)
{
	assert(fd == fixture_fd);
	return (0);
}

static off_t
test_lseek(int fd, off_t offset, int whence)
{
	assert(fd == fixture_fd);
	assert(whence == SEEK_SET);
	return (offset);
}

static ssize_t
test_read(int fd, void * buf, size_t nbytes)
{
	assert(fd == fixture_fd);
	memset(buf, 0x4b, nbytes);
	return ((ssize_t)nbytes);
}

static ssize_t
test_noeintr_write(int fd, const void * buf, size_t nbytes)
{
	assert(fd == fixture_fd);
	assert(buf != NULL);
	return ((ssize_t)nbytes);
}

void
libcperciva_warn(const char * format, ...)
{
	(void)format;
}

void
libcperciva_warnx(const char * format, ...)
{
	(void)format;
}

static void
reset_close(void)
{
	close_calls = 0;
	errno = 0;
}

int
main(void)
{
	uint8_t buf[8];
	size_t i;

	reset_close();
	assert(disk_syncdir("fixture") == 0);
	assert(close_calls == 1);

	reset_close();
	memset(buf, 0, sizeof(buf));
	assert(disk_read("fixture", 9, sizeof(buf), buf) == 0);
	assert(close_calls == 1);
	for (i = 0; i < sizeof(buf); i++)
		assert(buf[i] == 0x4b);

	reset_close();
	assert(disk_write("fixture", 0, sizeof(buf), buf, 0) == 0);
	assert(close_calls == 1);

	reset_close();
	assert(disk_write("fixture", 1, sizeof(buf), buf, 1) == 0);
	assert(close_calls == 1);

	puts("PASS: disk helpers never retry close after EINTR");
	return (0);
}

/*
 * Regression for storage_read retaining an elasticqueue record after unlock.
 *
 * The fixture mutates the queue record as the read lock is released.  The
 * real storage_read() must use the file number it observed while locked.
 */
#include <sys/types.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../lbs/storage.c"

struct elasticqueue {
	int placeholder;
};

static struct elasticqueue fake_queue;
static struct file_state slot;
static int locked;
static uint64_t observed_fnum;
static off_t observed_offset;
static size_t observed_nbytes;

struct elasticqueue *
elasticqueue_init(size_t reclen)
{
	(void)reclen;
	return (NULL);
}

int
elasticqueue_add(struct elasticqueue * q, const void * rec)
{
	(void)q;
	(void)rec;
	return (-1);
}

void
elasticqueue_delete(struct elasticqueue * q)
{
	(void)q;
}

size_t
elasticqueue_getlen(const struct elasticqueue * q)
{
	(void)q;
	return (1);
}

void *
elasticqueue_get(struct elasticqueue * q, size_t pos)
{
	assert(q == &fake_queue);
	assert(pos == 0);
	return (&slot);
}

void
elasticqueue_free(struct elasticqueue * q)
{
	(void)q;
}

struct elasticqueue *
storage_findfiles(const char * path)
{
	(void)path;
	return (NULL);
}

int
storage_util_readlock(struct storage_state * S)
{
	(void)S;
	assert(locked == 0);
	locked = 1;
	return (0);
}

int
storage_util_writelock(struct storage_state * S)
{
	(void)S;
	return (-1);
}

int
storage_util_unlock(struct storage_state * S)
{
	(void)S;
	assert(locked == 1);
	/*
	 * Model the entry being shifted/reused as soon as another worker can
	 * mutate the elastic queue.  storage_read must no longer consult slot.
	 */
	slot.start = 101;
	locked = 0;
	return (0);
}

char *
storage_util_mkpath(struct storage_state * S, uint64_t fnum)
{
	char * p;

	(void)S;
	observed_fnum = fnum;
	if ((p = malloc(2)) == NULL)
		abort();
	p[0] = 'x';
	p[1] = '\0';
	return (p);
}

int
disk_read(const char * path, off_t offset, size_t nbytes, uint8_t * buf)
{
	assert(strcmp(path, "x") == 0);
	observed_offset = offset;
	observed_nbytes = nbytes;
	memset(buf, 0x5a, nbytes);
	return (0);
}

int
disk_write(const char * path, int creat, size_t nbytes, const uint8_t * buf,
    int nosync)
{
	(void)path;
	(void)creat;
	(void)nbytes;
	(void)buf;
	(void)nosync;
	return (-1);
}

int
disk_syncdir(const char * path)
{
	(void)path;
	return (0);
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

int
main(void)
{
	struct storage_state S;
	uint8_t buf[16];
	size_t i;

	memset(&S, 0, sizeof(S));
	S.files = &fake_queue;
	S.blocklen = sizeof(buf);
	S.minblk = 100;
	S.nextblk = 104;
	S.latency = 0;
	slot.start = 100;
	slot.len = 4;

	assert(storage_read(&S, 102, buf) == 1);
	assert(locked == 0);
	assert(slot.start == 101);
	assert(observed_fnum == 100);
	assert(observed_offset == 2 * (off_t)sizeof(buf));
	assert(observed_nbytes == sizeof(buf));
	for (i = 0; i < sizeof(buf); i++)
		assert(buf[i] == 0x5a);

	puts("PASS: storage_read snapshots file number before unlock");
	return (0);
}

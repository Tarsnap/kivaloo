/*
 * Regression for dispatch_done retrying close forever on a hard error.
 */
#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int test_close(int);
#define close test_close
#include "../../lbs/dispatch.c"
#undef close

struct workctl {
	int id;
};

static struct workctl workers[2] = {{0}, {1}};
static int kill_calls;
static int cancel_calls;
static int close0_calls;
static int close1_calls;

static int
test_close(int fd)
{
	if (fd == 101)
		close0_calls++;
	else if (fd == 102)
		close1_calls++;
	else
		abort();

	/*
	 * A hard close failure must be recorded and returned, not retried.
	 * Abort promptly if the old loop calls us again instead of hanging.
	 */
	if ((close0_calls > 1) || (close1_calls > 1))
		abort();
	errno = EIO;
	return (-1);
}

void *
imalloc(size_t nrec, size_t reclen)
{
	(void)nrec;
	(void)reclen;
	return (NULL);
}

struct workctl *
worker_create(size_t ID, struct storage_state * S, int wakeupsock)
{
	(void)ID;
	(void)S;
	(void)wakeupsock;
	return (NULL);
}

int
worker_kill(struct workctl * W)
{
	assert((W == &workers[0]) || (W == &workers[1]));
	kill_calls++;
	return (0);
}

void *
network_accept(int fd, int (*callback)(void *, int), void * cookie)
{
	(void)fd;
	(void)callback;
	(void)cookie;
	return (NULL);
}

void *
network_read(int fd, uint8_t * buf, size_t buflen, size_t minread,
    int (*callback)(void *, ssize_t), void * cookie)
{
	(void)fd;
	(void)buf;
	(void)buflen;
	(void)minread;
	(void)callback;
	(void)cookie;
	return (NULL);
}

void
network_read_cancel(void * cookie)
{
	assert(cookie == (void *)0x1234);
	cancel_calls++;
}

struct netbuf_read *
netbuf_read_init(int fd)
{
	(void)fd;
	return (NULL);
}

void
netbuf_read_free(struct netbuf_read * R)
{
	(void)R;
}

struct netbuf_write *
netbuf_write_init(int fd, int (*callback)(void *), void * cookie)
{
	(void)fd;
	(void)callback;
	(void)cookie;
	return (NULL);
}

void
netbuf_write_free(struct netbuf_write * W)
{
	(void)W;
}

void *
wire_readpacket_wait(struct netbuf_read * R, int (*callback)(void *, int),
    void * cookie)
{
	(void)R;
	(void)callback;
	(void)cookie;
	return (NULL);
}

void
wire_readpacket_wait_cancel(void * cookie)
{
	(void)cookie;
}

int
proto_lbs_request_read(struct netbuf_read * R, struct proto_lbs_request * req)
{
	(void)R;
	(void)req;
	return (-1);
}

int
dispatch_response_send(struct dispatch_state * D, struct workctl * W)
{
	(void)D;
	(void)W;
	return (-1);
}

int
dispatch_request_params(struct dispatch_state * D, struct proto_lbs_request * R)
{
	(void)D;
	(void)R;
	return (-1);
}

int
dispatch_request_params2(struct dispatch_state * D,
    struct proto_lbs_request * R)
{
	(void)D;
	(void)R;
	return (-1);
}

int
dispatch_request_get(struct dispatch_state * D, struct proto_lbs_request * R)
{
	(void)D;
	(void)R;
	return (-1);
}

int
dispatch_request_pokereadq(struct dispatch_state * D)
{
	(void)D;
	return (-1);
}

int
dispatch_request_append(struct dispatch_state * D,
    struct proto_lbs_request * R)
{
	(void)D;
	(void)R;
	return (-1);
}

int
dispatch_request_free(struct dispatch_state * D, struct proto_lbs_request * R)
{
	(void)D;
	(void)R;
	return (-1);
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
	struct dispatch_state * D;

	if ((D = calloc(1, sizeof(*D))) == NULL)
		abort();
	if ((D->workers = malloc(2 * sizeof(*D->workers))) == NULL)
		abort();
	if ((D->readers_idle = malloc(sizeof(*D->readers_idle))) == NULL)
		abort();

	D->nreaders = 0;
	D->workers[0] = &workers[0];
	D->workers[1] = &workers[1];
	D->wakeup_cookie = (void *)0x1234;
	D->spair[0] = 101;
	D->spair[1] = 102;

	assert(dispatch_done(D) == -1);
	assert(kill_calls == 2);
	assert(cancel_calls == 1);
	assert(close0_calls == 1);
	assert(close1_calls == 1);

	puts("PASS: dispatch_done returns after one hard-error close per fd");
	return (0);
}

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int test_cond_signal_calls;
int test_pthread_cond_signal(pthread_cond_t *);

#define pthread_cond_signal test_pthread_cond_signal
#include "../lbs/worker.c"
#undef pthread_cond_signal

int
test_pthread_cond_signal(pthread_cond_t * cv)
{
	(void)cv;
	test_cond_signal_calls++;
	return (EINVAL);
}

static int test_dispatch_assign_calls;
static int test_dispatch_worker_assign(struct workctl *, int, uint64_t,
    size_t, uint8_t *, uint64_t);
static uint64_t test_storage_nextblock(struct storage_state *);

#define worker_assign test_dispatch_worker_assign
#define storage_nextblock test_storage_nextblock
#include "../lbs/dispatch_request.c"
#undef storage_nextblock
#undef worker_assign

static int
test_dispatch_worker_assign(struct workctl * ctl, int op, uint64_t blkno,
    size_t nblks, uint8_t * buf, uint64_t reqID)
{
	(void)ctl;
	(void)op;
	(void)blkno;
	(void)nblks;
	(void)buf;
	(void)reqID;
	test_dispatch_assign_calls++;
	return (-1);
}

static uint64_t
test_storage_nextblock(struct storage_state * S)
{
	(void)S;
	return (0);
}

static void
test_worker_assignment_rollback(void)
{
	struct workctl ctl;
	uint8_t buf = 0;
	int rc;

	memset(&ctl, 0, sizeof(ctl));
	assert(pthread_mutex_init(&ctl.mtx, NULL) == 0);
	assert(pthread_cond_init(&ctl.cv, NULL) == 0);

	test_cond_signal_calls = 0;
	assert(worker_assign(&ctl, 1, 7, 1, &buf, 9) == -1);
	assert(test_cond_signal_calls == 1);

	/*
	 * A failed signal means ownership did not transfer.  The worker must not
	 * observe the published tuple, and the caller must get the mutex back.
	 */
	assert(ctl.haswork == 0);
	assert(ctl.workdone == 0);
	rc = pthread_mutex_trylock(&ctl.mtx);
	assert(rc == 0);
	assert(pthread_mutex_unlock(&ctl.mtx) == 0);

	assert(pthread_cond_destroy(&ctl.cv) == 0);
	assert(pthread_mutex_destroy(&ctl.mtx) == 0);
}

static void
test_dispatch_busy_rollback(void)
{
	struct dispatch_state D;
	struct workctl * workers[2];
	struct proto_lbs_request * R;

	memset(&D, 0, sizeof(D));
	workers[0] = (struct workctl *)(uintptr_t)1;
	workers[1] = (struct workctl *)(uintptr_t)2;
	D.workers = workers;
	D.nreaders = 0;

	test_dispatch_assign_calls = 0;

	R = calloc(1, sizeof(*R));
	assert(R != NULL);
	R->r.append.blkno = 0;
	R->r.append.nblks = 1;
	R->r.append.buf = malloc(1);
	assert(R->r.append.buf != NULL);
	assert(dispatch_request_append(&D, R) == -1);
	assert(D.writer_busy == 0);

	R = calloc(1, sizeof(*R));
	assert(R != NULL);
	R->r.free.blkno = 1;
	assert(dispatch_request_free(&D, R) == -1);
	assert(D.deleter_busy == 0);

	assert(test_dispatch_assign_calls == 2);
}

int
main(void)
{
	test_worker_assignment_rollback();
	test_dispatch_busy_rollback();
	return (0);
}

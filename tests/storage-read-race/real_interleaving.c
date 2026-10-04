/* Reproduce kivaloo#320 with actual storage, queue, allocator and disk code.
 * GNU ld wrapping changes only scheduling: the reader releases the actual
 * pthread rwlock, then waits for an ordinary append on a second thread.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "storage.h"
#include "storage_internal.h"

static pthread_barrier_t released, appended;
static _Thread_local int pause_reader;
static struct storage_state * store;
static unsigned char readbuf[512];
static int read_status;
extern int __real_pthread_rwlock_unlock(pthread_rwlock_t *);

static void rendezvous(pthread_barrier_t *b) {
    int r = pthread_barrier_wait(b);
    assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
}
int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *lock) {
    int r = __real_pthread_rwlock_unlock(lock);
    if (r == 0 && pause_reader && lock == &store->lck) {
        pause_reader = 0;
        rendezvous(&released);
        rendezvous(&appended);
    }
    return r;
}
static void * read_first(void *ignored) {
    (void)ignored;
    pause_reader = 1;
    read_status = storage_read(store, 0, readbuf);
    return NULL;
}
int main(int argc, char **argv) {
    unsigned char a[512], b[512], check[512];
    pthread_t reader;
    assert(argc == 2);
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(a, 'A', sizeof(a));
    memset(b, 'B', sizeof(b));
    store = storage_init(argv[1], sizeof(a), 0, 0);
    assert(store != NULL);
    assert(storage_write(store, 0, 1, a) == 0);
    assert(pthread_barrier_init(&released, NULL, 2) == 0);
    assert(pthread_barrier_init(&appended, NULL, 2) == 0);
    assert(pthread_create(&reader, NULL, read_first, NULL) == 0);
    rendezvous(&released);
    puts("reader has released its actual rwlock; append block 1 now");
    assert(storage_write(store, 1, 1, b) == 0);
    puts("ordinary append completed; resume block-0 read");
    rendezvous(&appended);
    assert(pthread_join(reader, NULL) == 0);
    assert(read_status == 1 && memcmp(readbuf, a, sizeof(a)) == 0);
    assert(storage_read(store, 1, check) == 1 && memcmp(check, b, sizeof(b)) == 0);
    assert(storage_nextblock(store) == 2);
    assert(storage_done(store) == 0);
    assert(pthread_barrier_destroy(&released) == 0);
    assert(pthread_barrier_destroy(&appended) == 0);
    puts("PASS: both real blocks remain readable with exact original bytes");
    return 0;
}

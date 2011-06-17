#include <inttypes.h>
#include <pthread.h>
#include <string.h>

#include "asprintf.h"
#include "warnp.h"

#include "storage_internal.h"

#include "storage_util.h"

/**
 * storage_util_readlock(S):
 * Grab a read lock on the storage state ${S}.
 */
int
storage_util_readlock(struct storage_state * S)
{
	int rc;

	/* Try to grab the lock. */
	if ((rc = pthread_rwlock_rdlock(&S->lck)) != 0) {
		warn0("pthread_rwlock_rdlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_util_writelock(S):
 * Grab a write lock on the storage state ${S}.
 */
int
storage_util_writelock(struct storage_state * S)
{
	int rc;

	/* Try to grab the lock. */
	if ((rc = pthread_rwlock_wrlock(&S->lck)) != 0) {
		warn0("pthread_rwlock_wrlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_util_unlock(S):
 * Release the lock on the storage state ${S}.
 */
int
storage_util_unlock(struct storage_state * S)
{
	int rc;

	/* Try to release the lock. */
	if ((rc = pthread_rwlock_unlock(&S->lck)) != 0) {
		warn0("pthread_rwlock_unlock: %s", strerror(rc));
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * storage_util_mkpath(S, fileno):
 * Return the malloc-allocated NUL-terminated string "${dir}/blks_${fileno}"
 * where ${dir} is ${S}->storagedir and ${fileno} is a 0-padding hex value.
 */
char *
storage_util_mkpath(struct storage_state * S, uint64_t fileno)
{
	char * s;

	/* Construct path. */
	if (asprintf(&s, "%s/blks_%016" PRIx64,
	    S->storagedir, fileno) == -1) {
		warnp("asprintf");
		goto err0;
	}

	/* Success! */
	return (s);

err0:
	/* Failure! */
	return (NULL);
}

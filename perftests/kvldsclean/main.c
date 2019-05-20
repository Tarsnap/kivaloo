#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "events.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "sock.h"
#include "sysendian.h"
#include "warnp.h"
#include "wire.h"

struct many_state {
	size_t Nip;
	int failed;
	int done;
};

static int
callback_done(void * cookie, int failed)
{
	struct many_state * C = cookie;

	/* Did we fail? */
	if (failed)
		C->failed = 1;

	C->Nip -= 1;

	/* Are we done? */
	if (C->Nip == 0)
		C->done = 1;

	/* Success! */
	return (0);
}

static int
batch_set(struct wire_requestqueue * Q, size_t start, size_t N)
{
	struct many_state C;
	char buf[9];
	size_t x;
	struct kvldskey * key;

	/* Not failed or done yet.  No requests in progress yet. */
	C.failed = C.done = 0;
	C.Nip = 0;

	/* Set values. */
	for (x = start; x < start + N; x++) {
		/* Set "$x" = "$x". */
		sprintf(buf, "%08zu", x);
		if ((key = kvldskey_create((uint8_t *)buf, 8)) == NULL)
			goto err0;
		if (proto_kvlds_request_set(Q, key, key, callback_done, &C)) {
			warnp("Failed to send SET request");
			goto err1;
		}
		C.Nip += 1;
		kvldskey_free(key);
	}

	/* Spin until the requests are done. */
	if (events_spin(&C.done) || C.failed) {
		warnp("SET request failed");
		return (-1);
	}

	/* Success! */
	return (0);

err1:
	kvldskey_free(key);
err0:
	/* Failure! */
	return (-1);
}

static int
batch_delete(struct wire_requestqueue * Q, size_t start, size_t N)
{
	struct many_state C;
	char buf[9];
	size_t x;
	struct kvldskey * key;

	/* Not failed or done yet.  No requests in progress yet. */
	C.failed = C.done = 0;
	C.Nip = 0;

	/* Set values. */
	for (x = start; x < start + N; x++) {
		/* Delete "$x". */
		sprintf(buf, "%08zu", x);
		if ((key = kvldskey_create((uint8_t *)buf, 8)) == NULL)
			goto err0;
		if (proto_kvlds_request_delete(Q, key, callback_done, &C)) {
			warnp("Failed to send DELETE request");
			goto err1;
		}
		C.Nip += 1;
		kvldskey_free(key);
	}

	/* Spin until the requests are done. */
	if (events_spin(&C.done) || C.failed) {
		warnp("DELETE request failed");
		return (-1);
	}

	/* Success! */
	return (0);

err1:
	kvldskey_free(key);
err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct sock_addr ** sas;
	int s;
	struct wire_requestqueue * Q;
	size_t i;
	time_t t_now;
	char datetime[20];

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_kvlds %s\n", "<socketname>");
		exit(1);
	}

	/* Resolve the socket address and connect. */
	if ((sas = sock_resolve(argv[1])) == NULL) {
		warnp("Error resolving socket address: %s", argv[1]);
		exit(1);
	}
	if (sas[0] == NULL) {
		warn0("No addresses found for %s", argv[1]);
		exit(1);
	}
	if ((s = sock_connect(sas)) == -1)
		exit(1);

	/* Create a request queue. */
	if ((Q = wire_requestqueue_init(s)) == NULL) {
		warnp("Cannot create packet write queue");
		exit(1);
	}

	/* 10 stages of writing new keys followed by existing keys. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: Writing and modifying...", datetime);
	for (i = 0; i < 10; i++) {
		/* Write 1k new keys. */
		if (batch_set(Q, i * 1000, 1000)) {
			warnp("Failure writing new keys");
			exit(1);
		}

		/* Write the same 100k keys each iteration. */
		if (batch_set(Q, 1000000, 100000)) {
			warnp("Failure writing old keys");
			exit(1);
		}
	}

	/* Sleep 1800 seconds so that we can watch the cleaner kick in. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: ... done", datetime);
	sleep(1800);

	/* Delete the 100k repeatedly modified keys. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: Deleting repeatedly modified keys...", datetime);
	if (batch_delete(Q, 1000000, 100000)) {
		warnp("Failure deleting old keys");
		exit(1);
	}

	/* Sleep 1200 seconds so that we can watch the cleaner do more work. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: ... done", datetime);
	sleep(1200);

	/* Delete the 10 x 1k once-written keys. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: Deleting once-written keys...", datetime);
	if (batch_delete(Q, 0, 10000)) {
		warnp("Failure deleting new keys");
		exit(1);
	}

	/* Sleep 10 seconds so that we can see an empty tree. */
	time(&t_now);
	strftime(datetime, 20, "%F %T", gmtime(&t_now));
	warn0("%s: ... done", datetime);
	sleep(10);

	/* Free the request queue. */
	wire_requestqueue_destroy(Q);
	wire_requestqueue_free(Q);

	/* Free socket addresses. */
	sock_addr_freelist(sas);

	/* Shut down the event subsystem. */
	events_shutdown();

	/* Success! */
	exit(0);
}

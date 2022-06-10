#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kivaloo.h"
#include "kvlds.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "sysendian.h"
#include "warnp.h"

static int
set_get_blocking(struct wire_requestqueue * Q)
{
	struct kvldskey * key;
	struct kvldskey * val;
	struct kvldskey * res;
	const char * key_str = "key";
	const char * val_str = "val";

	/* Create pair to store. */
	key = kvldskey_create((const uint8_t *)key_str, strlen(key_str));
	val = kvldskey_create((const uint8_t *)val_str, strlen(val_str));

	/* Store pair. */
	if (kvlds_set(Q, key, val))
		goto err0;

	/* Retrieve value. */
	if (kvlds_get(Q, key, &res))
		goto err0;

	/* Check that it matches the expected one. */
	if (val->len != res->len)
		goto err0;
	if (memcmp(res->buf, val->buf, val->len))
		goto err0;

	/* Clean up. */
	kvldskey_free(key);
	kvldskey_free(val);
	kvldskey_free(res);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct wire_requestqueue * Q;
	struct kivaloo_cookie * K;

	WARNP_INIT;

	/* Check number of arguments. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_kvlds %s\n", "<socketname>");
		exit(1);
	}

	/* Open a connection to KVLDS. */
	if ((K = kivaloo_open(argv[1], &Q)) == NULL) {
		warnp("Could not connect o KVLDS daemon.");
		exit(1);
	}

	/* Test the blocking API. */
	if (set_get_blocking(Q))
		exit(1);

	/* Free the request queue and network connection. */
	kivaloo_close(K);

	/* Success! */
	exit(0);
}

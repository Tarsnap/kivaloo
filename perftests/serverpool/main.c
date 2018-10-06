#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "serverpool.h"
#include "sock.h"
#include "sock_util.h"
#include "warnp.h"

static int done;
static int ticksleft;

static int
callback_tick(void * cookie)
{
	struct serverpool * SP = cookie;
	struct sock_addr * sa;
	char * s;

	/* Get an address from the pool. */
	if ((sa = serverpool_pick(SP)) == NULL) {
		warnp("serverpool_pick");
		goto err0;
	}

	/* Pretty-print it. */
	if ((s = sock_addr_prettyprint(sa)) == NULL) {
		warnp("sock_addr_prettyprint");
		goto err1;
	}
	printf("%s\n", s);

	/* Free string and address. */
	free(s);
	sock_addr_free(sa);

	/* Are we done yet? */
	if (--ticksleft > 0) {
		/* Schedule another callback. */
		if (events_timer_register_double(callback_tick, SP, 1)
		    == NULL) {
			warnp("events_timer_register_double");
			goto err0;
		}
	} else {
		/* We're done. */
		done = 1;
	}

	/* Success! */
	return (0);

err1:
	sock_addr_free(sa);
err0:
	/* Failure! */
	return (-1);
}

int
main(int argc, char * argv[])
{
	struct serverpool * SP;

	WARNP_INIT;

	/* Sanity-check. */
	if (argc != 2) {
		fprintf(stderr, "usage: test_serverpool %s\n", "<target>");
		exit(1);
	}

	/* Fork off a process to perform DNS lookups. */
	if ((SP = serverpool_create(argv[1], 5, 30)) == NULL) {
		warnp("Error launching DNS lookups");
		exit(1);
	}

	/* Call back every second for 2 minutes. */
	ticksleft = 120;
	done = 0;
	if (events_timer_register_double(callback_tick, SP, 1) == NULL) {
		warnp("events_timer_register_double");
		exit(1);
	}

	/* Wait until we're done. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Shut down the DNS lookups. */
	serverpool_free(SP);

	/* Shut down events loop (in case we're checking for memory leaks). */
	events_shutdown();

	/* Success! */
	exit(0);
}

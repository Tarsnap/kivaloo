#include <stdio.h>
#include <stdlib.h>

#include "asprintf.h"
#include "events.h"
#include "http.h"
#include "sock.h"
#include "warnp.h"

static int
donereq(void * cookie, struct http_response * R)
{
	int * done = cookie;
	size_t i;

	/* This request is over. */
	*done = 1;

	/* Did we succeed? */
	if (R == NULL) {
		warn0("HTTP request failed");
		return (-1);
	}

	/* Print status and headers. */
	printf("HTTP status = %d\n", R->status);
	for (i = 0; i < R->nheaders; i++)
		printf("%s\n\t%s\n",
		    R->headers[i].header, R->headers[i].value);

	/* Print the body received. */
	fwrite(R->body, 1, R->bodylen, stdout);

	/* Free the body. */
	free(R->body);

	/* Success! */
	return (0);
}

int
main(int argc, char * argv[])
{
	struct http_header HH[1];
	struct http_request R;
	char * s;
	struct sock_addr ** sas;
	int done = 0;

	WARNP_INIT;

	/* Sanity-check. */
	if (argc != 3) {
		warn0("Need two arguments (host, path)");
		exit(1);
	}

	/* Construct headers. */
	HH[0].header = "Host";
	HH[0].value = argv[1];

	/* Construct request. */
	R.method = "GET";
	R.path = argv[2];
	R.nheaders = 1;
	R.headers = HH;
	R.bodylen = 0;
	R.body = NULL;

	/* Resolve target addresses. */
	if (asprintf(&s, "%s:80", argv[1]) == -1)
		exit(1);
	if ((sas = sock_resolve(s)) == NULL) {
		warnp("Cannot resolve %s", s);
		exit(1);
	}
	free(s);

	/* Send request. */
	(void)http_request(sas, &R, 1000000, donereq, &done);

	/* Wait for request to complete. */
	if (events_spin(&done)) {
		warnp("Error in event loop");
		exit(1);
	}

	/* Free address structures. */
	sock_addr_freelist(sas);

	/* Shut down events loop (in case we're checking for memory leaks). */
	events_shutdown();

	return (0);
}

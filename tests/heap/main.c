#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ptrheap.h"

#define MAX_LINE_LENGTH 128

static int
compar(void * cookie, const void * x, const void * y)
{
	const char * _x = x;
	const char * _y = y;

	(void)cookie; /* UNUSED */

	return (strcmp(_x, _y));
}

int
main(int argc, char * argv[])
{
	struct ptrheap * H;
	char * s, * dups;
	char input[MAX_LINE_LENGTH];

	(void)argc; /* UNUSED */
	(void)argv; /* UNUSED */

	/* Create a heap. */
	if ((H = ptrheap_init(compar, NULL, NULL)) == 0)
		exit(1);

	/* Suck in the input. */
	while (fgets(input, MAX_LINE_LENGTH, stdin) != NULL) {
		/* Remove final newline. */
		input[strlen(input)-1] = '\0';

		/* Duplicate string. */
		if ((dups = strdup(input)) == NULL)
			exit(1);

		/* Insert string. */
		if (ptrheap_add(H, dups))
			exit(1);
	};

	/* Write out the lines in order. */
	while ((s = ptrheap_getmin(H)) != NULL) {
		printf("%s\n", s);
		ptrheap_deletemin(H);
		free(s);
	}

	/* Free the heap. */
	ptrheap_free(H);

	return (0);
}

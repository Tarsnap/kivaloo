#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ptrheap.h"

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
	size_t l;

	(void)argc; /* UNUSED */
	(void)argv; /* UNUSED */

	/* Create a heap. */
	if ((H = ptrheap_init(compar, NULL, NULL)) == 0)
		exit(1);

	/* Suck in the input. */
	while ((s = fgetln(stdin, &l)) != NULL) {
		/* NUL-terminate. */
		s[l-1] = '\0';

		/* Duplicate string. */
		if ((dups = strdup(s)) == NULL)
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

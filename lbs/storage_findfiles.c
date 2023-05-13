#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "asprintf.h"
#include "elasticqueue.h"
#include "hexify.h"
#include "ptrheap.h"
#include "sysendian.h"
#include "warnp.h"

#include "storage_findfiles.h"

/* Compare the fileno values in two storage_file structures. */
static int
fs_compar(void * cookie, const void * _x, const void * _y)
{
	const struct storage_file * x = _x;
	const struct storage_file * y = _y;

	(void)cookie; /* UNUSED */
	if (x->fileno < y->fileno)
		return (-1);
	else if (x->fileno > y->fileno)
		return (1);
	else
		return (0);
}

/**
 * storage_findfiles(path):
 * Look for files named "blks_<16 hex digits>" in the directory ${path}.
 * Return an elastic queue of struct storage_file, in order of increasing
 * fileno.
 */
struct elasticqueue *
storage_findfiles(const char * path)
{
	struct stat sb;
	DIR * dir;
	struct dirent * dp;
	struct ptrheap * H;
	struct storage_file * sf;
	char * s;
	uint8_t fileno_exp[8];
	struct elasticqueue * Q;

	/* Create a heap for holding storage_file structures. */
	if ((H = ptrheap_init(fs_compar, NULL, NULL)) == NULL)
		goto err0;

	/* Create a queue for holding the structures in sorted order. */
	if ((Q = elasticqueue_init(sizeof(struct storage_file))) == NULL)
		goto err1;

	/* Open the storage directory. */
	if ((dir = opendir(path)) == NULL) {
		warnp("Cannot open storage directory: %s", path);
		goto err2;
	}

	/*
	 * Look for files named "blks_<64-bit hexified first block #>" and
	 * create storage_file structures for each.
	 */
	while (1) {
		/* Get a pointer to the next directory entry. */
		errno = 0;
		if ((dp = readdir(dir)) == NULL)
			break;

		/* Skip anything which isn't the right length. */
		if (strlen(dp->d_name) != strlen("blks_0123456789abcdef"))
			continue;

		/* Skip anything which doesn't start with "blks_". */
		if (strncmp(dp->d_name, "blks_", 5))
			continue;

		/* Make sure the name has 8 hexified bytes and parse. */
		if (unhexify(&dp->d_name[5], fileno_exp, 8))
			continue;

		/* Construct a full path to the file and stat. */
		if (asprintf(&s, "%s/%s", path, dp->d_name) == -1) {
			warnp("asprintf");
			goto err3;
		}
		if (lstat(s, &sb)) {
			warnp("stat(%s)", s);
			goto err4;
		}

		/* Skip anything other than regular files. */
		if (!S_ISREG(sb.st_mode))
			goto next;

		/* Allocate a file_state structure. */
		if ((sf = malloc(sizeof(struct storage_file))) == NULL)
			goto err4;

		/* Fill in file number and size. */
		sf->fileno = be64dec(fileno_exp);
		sf->len = sb.st_size;

		/* Insert the file into the heap. */
		if (ptrheap_add(H, sf))
			goto err5;

next:
		/* Free the full path to the file. */
		free(s);
	}
	if (errno != 0) {
		warnp("Error reading storage directory: %s", path);
		goto err3;
	}

	/* Close the storage directory. */
	while (closedir(dir)) {
		/* Retry if we were interrupted. */
		if (errno == EINTR)
			continue;

		/* Oops, something bad happened. */
		warnp("Error closing storage directory: %s", path);
		goto err2;
	}

	/* Suck structures from the heap into the queue. */
	while ((sf = ptrheap_getmin(H)) != NULL) {
		if (elasticqueue_add(Q, sf))
			goto err2;
		free(sf);
		ptrheap_deletemin(H);
	}

	/* Free the (now empty) heap. */
	ptrheap_free(H);

	/* Success! */
	return (Q);

err5:
	free(sf);
err4:
	free(s);
err3:
	closedir(dir);
err2:
	elasticqueue_free(Q);
err1:
	while ((sf = ptrheap_getmin(H)) != NULL) {
		ptrheap_deletemin(H);
		free(sf);
	}
	ptrheap_free(H);
err0:
	/* Failure! */
	return (NULL);
}

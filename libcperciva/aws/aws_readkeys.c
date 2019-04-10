#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "insecure_memzero.h"
#include "warnp.h"

#include "aws_readkeys.h"

/**
 * aws_readkeys(fname, key_id, key_secret):
 * Read an AWS key id and secret from the file ${fname}, returning malloced
 * strings via ${key_id} and ${key_secret}.
 */
int
aws_readkeys(const char * fname, char ** key_id, char ** key_secret)
{
	FILE * f;
	char buf[1024];
	char * p;

	/* No keys yet. */
	*key_id = *key_secret = NULL;

	/* Open the key file. */
	if ((f = fopen(fname, "r")) == NULL) {
		warnp("fopen(%s)", fname);
		goto err0;
	}

	/* Read lines of up to 1024 characters. */
	while (fgets(buf, sizeof(buf), f) != NULL) {
		/* Find the first EOL character and truncate. */
		p = buf + strcspn(buf, "\r\n");
		if (*p == '\0') {
			warn0("Missing EOL in %s", fname);
			break;
		} else
			*p = '\0';

		/* Look for the first = character. */
		p = strchr(buf, '=');

		/* Missing separator? */
		if (p == NULL)
			goto err3;

		/* Replace separator with NUL and point p at the value. */
		*p++ = '\0';

		/* We should have ACCESS_KEY_ID or ACCESS_KEY_SECRET. */
		if (strcmp(buf, "ACCESS_KEY_ID") == 0) {
			/* Copy key ID string. */
			if (*key_id != NULL) {
				warn0("ACCESS_KEY_ID specified twice");
				goto err2;
			}
			if ((*key_id = strdup(p)) == NULL)
				goto err2;
		} else if (strcmp(buf, "ACCESS_KEY_SECRET") == 0) {
			/* Copy key secret string. */
			if (*key_secret != NULL) {
				warn0("ACCESS_KEY_SECRET specified twice");
				goto err2;
			}
			if ((*key_secret = strdup(p)) == NULL)
				goto err2;
		} else
			goto err3;
	}

	/* Check for error. */
	if (ferror(f)) {
		warnp("Error reading %s", fname);
		goto err2;
	}

	/* Close the file. */
	if (fclose(f)) {
		warnp("fclose");
		goto err1;
	}

	/* Check that we got the necessary keys. */
	if ((*key_id == NULL) || (*key_secret == NULL)) {
		warn0("Need ACCESS_KEY_ID and ACCESS_KEY_SECRET");
		goto err1;
	}

	/* Success! */
	return (0);

err3:
	warn0("Lines in %s must be ACCESS_KEY_(ID|SECRET)=...", fname);
err2:
	fclose(f);
err1:
	free(*key_id);
	if (*key_secret) {
		insecure_memzero(*key_secret, strlen(*key_secret));
		free(*key_secret);
	}
err0:
	/* Failure! */
	return (-1);
}

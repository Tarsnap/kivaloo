#ifndef _AWS_READKEYS_H_
#define _AWS_READKEYS_H_

/**
 * aws_readkeys(fname, key_id, key_secret):
 * Read an AWS key id and secret from the file ${fname}, returning malloced
 * strings via ${key_id} and ${key_secret}.
 */
int aws_readkeys(const char *, char **, char **);

#endif /* !_AWS_READKEYS_H_ */

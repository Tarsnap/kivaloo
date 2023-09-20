#include <openssl/ssl.h>

int
main(void)
{
	const char * hostname = "myhostname";

	(void)SSL_set_tlsext_host_name(NULL, hostname);

	/* Success! */
	return (0);
}

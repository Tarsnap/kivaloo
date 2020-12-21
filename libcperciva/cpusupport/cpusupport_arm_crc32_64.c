#include "cpusupport.h"

#ifdef CPUSUPPORT_HWCAP_GETAUXVAL
#include <sys/auxv.h>
#endif /* CPUSUPPORT_HWCAP_GETAUXVAL */

CPUSUPPORT_FEATURE_DECL(arm, crc32_64)
{
	int supported = 0;

#if defined(CPUSUPPORT_ARM_CRC32_64)
#if defined(CPUSUPPORT_HWCAP_GETAUXVAL)
	unsigned long capabilities;

#if defined(__aarch64__)
	capabilities = getauxval(AT_HWCAP);
	supported = (capabilities & HWCAP_CRC32) ? 1 : 0;
#endif
#endif /* CPUSUPPORT_HWCAP_GETAUXVAL */
#endif /* CPUSUPPORT_ARM_CRC32_64 */

	/* Return the supported status. */
	return (supported);
}

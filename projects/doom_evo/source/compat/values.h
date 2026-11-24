/* compat/values.h - the SDP's QNX 6.5 headers (and gcc 4.4.2) don't ship the
 * legacy SVID <values.h>; the retired Linaro gcc bundled it. DOOM includes it
 * for MININT/MAXINT etc. Map them to <limits.h>. On the -I path via -Icompat. */
#ifndef EVO_COMPAT_VALUES_H
#define EVO_COMPAT_VALUES_H
#include <limits.h>
#ifndef MAXINT
#define MAXINT   INT_MAX
#endif
#ifndef MININT
#define MININT   INT_MIN
#endif
#ifndef MAXSHORT
#define MAXSHORT SHRT_MAX
#endif
#ifndef MINSHORT
#define MINSHORT SHRT_MIN
#endif
#ifndef MAXLONG
#define MAXLONG  LONG_MAX
#endif
#ifndef MINLONG
#define MINLONG  LONG_MIN
#endif
#endif /* EVO_COMPAT_VALUES_H */

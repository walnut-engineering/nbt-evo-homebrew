/*
 * qnx_prefix.h - forced (-include) before every translation unit on QNX.
 *
 * QNX's <sys/types.h> does `typedef _Uint16t msg_t;` (a 16-bit id for native
 * QNX message passing). quake3e's qcommon.h defines its own `msg_t` struct
 * (the network message buffer), used pervasively across the engine. The two
 * collide. quake3e never uses QNX message passing, so we process <sys/types.h>
 * first with QNX's `msg_t` renamed aside to `qnx_msg_t`, then free the name
 * for quake3e. Being pulled in via `-include` guarantees this runs before the
 * TU's own headers no matter where q_shared.h includes <sys/types.h>.
 *
 * This is port glue, not an upstream edit, so it lives here (tracked) and is
 * referenced from build.sh rather than patched into the quake3e tree.
 */
#ifndef QUAKE3E_QNX_PREFIX_H
#define QUAKE3E_QNX_PREFIX_H

#ifdef __QNXNTO__
#define msg_t qnx_msg_t
#include <sys/types.h>
#undef msg_t
#endif

#endif /* QUAKE3E_QNX_PREFIX_H */

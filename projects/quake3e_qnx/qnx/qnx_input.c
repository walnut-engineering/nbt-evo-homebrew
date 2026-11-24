/*
 * qnx_input.c - QNX (NBT EVO) input layer for quake3e, replacing
 * code/sdl/sdl_input.c.
 *
 * Reads a USB keyboard (/dev/usbkbd0) and mouse (/dev/usbmouse0) through the
 * unit's devu-kbd / devu-mouse resource managers, the same proven path DOOM
 * uses on this head unit. Each device gets its own blocking reader thread; the
 * threads push decoded events into a small mutex-protected ring, and
 * HandleEvents() (called on the main/client thread from IN_Frame) drains the
 * ring into the engine via Com_QueueEvent (= Sys_QueEvent, which is NOT thread
 * safe, hence the drain-on-main-thread design).
 *
 * devu-kbd / devu-mouse QUIRK: they ignore O_NONBLOCK and don't support
 * select()/readcond(); a thread reply-blocked in read() even keeps the process
 * from exiting. A kernel-side TimerTimeout bounds each read (returns EINTR on
 * timeout) so the threads can poll the shutdown flag and the game can quit.
 *
 * Keyboard: PC "scancode set 1" bytes, bit 0x80 = key-up. Mouse: devu-mouse
 * boot-protocol packets (byte0 = 0x40|flags, byte1=|dx|, byte2=|dy|).
 *
 * The iDrive knob is a THIRD source (ctrl_thread, below): it consumes the DSI
 * MoCCA controller feed ring /dev/shmem/evo_ctrl_feed (produced by evo_ctrlfeed
 * / the `most` tee, schema = controller/evo_feed.h v3, the same ring DOOM uses),
 * holds the controller GRAB so the hidden OEM HMI ignores the knob, and maps
 * rotary->turn, 5-way->move/strafe, centre->fire, BACK->confirm-quit, plus weapon /
 * jump / scoreboard buttons. Gameplay actions go out as binding-independent
 * console commands (+forward / +attack / weapnext ...) via SE_CONSOLE; turn
 * goes out as relative mouse. If the ring is absent the thread just idles, so
 * the game still runs on keyboard + mouse alone.
 */
#include "client.h"
#include "qnx_glw.h"
#include "keycodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/neutrino.h>

#define QNX_KBD_DEV     "/dev/usbkbd0"
#define QNX_MOUSE_DEV   "/dev/usbmouse0"
#define QNX_READ_TMO_MS 200

/* ---- kernel-timeout-bounded blocking read (devu-* ignore O_NONBLOCK) ----- */
static ssize_t bounded_read( int fd, void *buf, size_t n )
{
	uint64_t to = (uint64_t)QNX_READ_TMO_MS * 1000000ULL;
	TimerTimeout( CLOCK_MONOTONIC, _NTO_TIMEOUT_REPLY | _NTO_TIMEOUT_SEND, NULL, &to, NULL );
	return read( fd, buf, n );
}

/* ---- producer->consumer event ring --------------------------------------- */
/* IE_CONSOLE carries a console command string (iDrive -> +forward/+attack/...);
 * HandleEvents turns it into an SE_CONSOLE event on the main thread. */
typedef enum { IE_KEY, IE_CHAR, IE_MOUSE, IE_CONSOLE } ieType_t;
typedef struct { ieType_t type; int v1, v2; char cmd[32]; } inEvent_t;

#define INEV_RING 512
static inEvent_t      s_ring[INEV_RING];
static unsigned       s_head, s_tail;                 /* head = producer, tail = consumer */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static void push_event( ieType_t type, int v1, int v2 )
{
	pthread_mutex_lock( &s_lock );
	if ( s_head - s_tail < INEV_RING ) {
		inEvent_t *e = &s_ring[ s_head % INEV_RING ];
		e->type = type; e->v1 = v1; e->v2 = v2;
		s_head++;
	}
	pthread_mutex_unlock( &s_lock );
}

/* queue a console command (iDrive controller -> engine, main-thread dispatch) */
static void push_cmd( const char *s )
{
	pthread_mutex_lock( &s_lock );
	if ( s_head - s_tail < INEV_RING ) {
		inEvent_t *e = &s_ring[ s_head % INEV_RING ];
		e->type = IE_CONSOLE; e->v1 = e->v2 = 0;
		strncpy( e->cmd, s, sizeof( e->cmd ) - 1 );
		e->cmd[ sizeof( e->cmd ) - 1 ] = '\0';
		s_head++;
	}
	pthread_mutex_unlock( &s_lock );
}

/* ---- device threads ------------------------------------------------------ */
static volatile int s_shutdown = 0;
static int          s_kbd_fd = -1, s_mouse_fd = -1;
static pthread_t    s_kbd_tid, s_mouse_tid;
static int          s_kbd_running = 0, s_mouse_running = 0;
static int          s_shift = 0;                      /* keyboard shift state (for SE_CHAR) */

/* scancode (set 1, low 7 bits) -> quake3 keynum. Printable keys return their
 * lowercase ASCII (which IS the quake3 keynum); specials return K_*. 0 = ignore
 * (also catches the 0xE0 extended prefix, which harmlessly maps to nothing). */
static int scan_to_key( unsigned char sc )
{
	switch ( sc ) {
		case 0x01: return K_ESCAPE;
		case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
		case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
		case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
		case 0x0B: return '0';
		case 0x0C: return '-'; case 0x0D: return '=';
		case 0x0E: return K_BACKSPACE;
		case 0x0F: return K_TAB;
		case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
		case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
		case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
		case 0x19: return 'p';
		case 0x1A: return '['; case 0x1B: return ']';
		case 0x1C: return K_ENTER;
		case 0x1D: return K_CTRL;
		case 0x1E: return 'a'; case 0x1F: return 's'; case 0x20: return 'd';
		case 0x21: return 'f'; case 0x22: return 'g'; case 0x23: return 'h';
		case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
		case 0x27: return ';'; case 0x28: return '\''; case 0x29: return '`';
		case 0x2A: return K_SHIFT;                 /* left shift */
		case 0x2B: return '\\';
		case 0x2C: return 'z'; case 0x2D: return 'x'; case 0x2E: return 'c';
		case 0x2F: return 'v'; case 0x30: return 'b'; case 0x31: return 'n';
		case 0x32: return 'm';
		case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/';
		case 0x36: return K_SHIFT;                 /* right shift */
		case 0x37: return K_KP_STAR;
		case 0x38: return K_ALT;
		case 0x39: return K_SPACE;
		case 0x3A: return K_CAPSLOCK;
		case 0x3B: return K_F1; case 0x3C: return K_F2; case 0x3D: return K_F3;
		case 0x3E: return K_F4; case 0x3F: return K_F5; case 0x40: return K_F6;
		case 0x41: return K_F7; case 0x42: return K_F8; case 0x43: return K_F9;
		case 0x44: return K_F10;
		case 0x57: return K_F11; case 0x58: return K_F12;
		case 0x47: return K_HOME;      case 0x48: return K_UPARROW;
		case 0x49: return K_PGUP;      case 0x4B: return K_LEFTARROW;
		case 0x4D: return K_RIGHTARROW; case 0x4F: return K_END;
		case 0x50: return K_DOWNARROW; case 0x51: return K_PGDN;
		case 0x52: return K_INS;       case 0x53: return K_DEL;
		default:   return 0;
	}
}

/* shifted ASCII for SE_CHAR (US layout) */
static int shift_char( int c )
{
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 'A';
	switch ( c ) {
		case '1': return '!'; case '2': return '@'; case '3': return '#';
		case '4': return '$'; case '5': return '%'; case '6': return '^';
		case '7': return '&'; case '8': return '*'; case '9': return '(';
		case '0': return ')'; case '-': return '_'; case '=': return '+';
		case '[': return '{'; case ']': return '}'; case '\\': return '|';
		case ';': return ':'; case '\'': return '"'; case '`': return '~';
		case ',': return '<'; case '.': return '>'; case '/': return '?';
	}
	return c;
}

static void *kbd_thread( void *arg )
{
	unsigned char buf[32];
	(void)arg;
	while ( !s_shutdown ) {
		ssize_t n;
		if ( s_kbd_fd < 0 ) { s_kbd_fd = open( QNX_KBD_DEV, O_RDONLY ); if ( s_kbd_fd < 0 ) { usleep( 500000 ); continue; } }
		errno = 0;
		n = bounded_read( s_kbd_fd, buf, sizeof( buf ) );
		if ( n <= 0 ) {
			if ( n < 0 && errno != EINTR && errno != EAGAIN ) { close( s_kbd_fd ); s_kbd_fd = -1; usleep( 200000 ); }
			continue;
		}
		{
			ssize_t i;
			for ( i = 0; i < n; i++ ) {
				unsigned char sc = buf[i];
				int down = !( sc & 0x80 );
				int key  = scan_to_key( sc & 0x7F );
				if ( !key ) continue;
				if ( key == K_SHIFT ) s_shift = down;
				push_event( IE_KEY, key, down );
				/* printable -> also emit a character for console/chat on key-down */
				if ( down && key >= 32 && key < 127 )
					push_event( IE_CHAR, s_shift ? shift_char( key ) : key, 0 );
			}
		}
	}
	return NULL;
}

static void *mouse_thread( void *arg )
{
	unsigned char buf[64];
	int plen = 0, lastbtn = 0;
	(void)arg;
	while ( !s_shutdown ) {
		ssize_t n, off;
		if ( s_mouse_fd < 0 ) { s_mouse_fd = open( QNX_MOUSE_DEV, O_RDONLY ); if ( s_mouse_fd < 0 ) { usleep( 500000 ); continue; } }
		errno = 0;
		n = bounded_read( s_mouse_fd, buf, sizeof( buf ) );
		if ( n <= 0 ) {
			if ( n < 0 && errno != EINTR && errno != EAGAIN ) { close( s_mouse_fd ); s_mouse_fd = -1; usleep( 200000 ); }
			continue;
		}
		if ( plen == 0 ) plen = ( n % 3 == 0 ) ? 3 : ( n % 4 == 0 ) ? 4 : 3;
		/* devu-mouse 3-byte packet on this unit (bench-decoded 2026-11-11):
		 *   byte0 = 0x40 sync | buttons | redundant motion-sign bits
		 *           bit5(0x20)=LEFT  bit4(0x10)=RIGHT  bit1-only(0x02,bit0 clear)=MIDDLE
		 *           bit0+bit1(0x03)=dx<0 sign, bit2+bit3(0x0c)=dy<0 sign (ignored - we
		 *           use the deltas' own sign below)
		 *   byte1 = dx, byte2 = dy, each 6-bit two's complement (0x20..0x3f = negative) */
		for ( off = 0; off + plen <= n; off += plen ) {
			unsigned char b0 = buf[off];
			int dx = buf[off + 1] & 0x3f, dy = buf[off + 2] & 0x3f, btn;
			if ( dx & 0x20 ) dx -= 0x40;
			if ( dy & 0x20 ) dy -= 0x40;
			if ( dx || dy ) push_event( IE_MOUSE, dx, dy );   /* dy+ = down = look down */
			btn = ( ( b0 & 0x20 ) ? 1 : 0 )                                  /* left  -> MOUSE1 */
			    | ( ( b0 & 0x10 ) ? 2 : 0 )                                  /* right -> MOUSE2 */
			    | ( ( ( b0 & 0x02 ) && !( b0 & 0x01 ) ) ? 4 : 0 );           /* middle-> MOUSE3 */
			if ( ( btn ^ lastbtn ) & 1 ) push_event( IE_KEY, K_MOUSE1, btn & 1 );
			if ( ( btn ^ lastbtn ) & 2 ) push_event( IE_KEY, K_MOUSE2, ( btn >> 1 ) & 1 );
			if ( ( btn ^ lastbtn ) & 4 ) push_event( IE_KEY, K_MOUSE3, ( btn >> 2 ) & 1 );
			lastbtn = btn;
		}
	}
	return NULL;
}

/* =====================  iDrive controller (DSI feed ring)  ================= */
/* Canonical schema = controller/evo_feed.h (v3); duplicated here so this file
 * is self-contained (same approach as DOOM's i_input_evo.c). MUST stay in sync. */
#define EVO_FEED_PATH   "/dev/shmem/evo_ctrl_feed"
#define EVO_FEED_RING   512u
#define EVO_FEED_MAGIC  0x0e5feed3u
#define EVO_K_ROTARY    1      /* (signed char)b[0] = detent delta; + = clockwise */
#define EVO_K_DIR       2      /* b[0]=EVO_DIR_* b[1]=1down/0up b[2]=raw tilt code */
#define EVO_K_CENTER    3      /* b[1]=1down/0up (knob pressed straight down)      */
#define EVO_K_BUTTON    4      /* b[0]=button id bit, b[1]=1down/0up               */
#define EVO_DIR_FWD     0
#define EVO_DIR_BACK    1
#define EVO_DIR_LEFT    2
#define EVO_DIR_RIGHT   3
#define EVO_GRAB_TTL_MS 2000u

struct evo_ev   { unsigned char kind, ctr, b[6]; };
struct evo_feed { volatile unsigned int magic, head;
                  struct evo_ev ev[EVO_FEED_RING];
                  volatile unsigned int grab_until, tee_alive; };

static pthread_t s_ctrl_tid;
static int       s_ctrl_running = 0;
static int       s_turn_scale   = 100;   /* detent delta * this -> mouse dx (env EVO_Q3_TURN) */

static unsigned int ms_now( void )
{
	struct timespec t; clock_gettime( CLOCK_MONOTONIC, &t );
	return (unsigned int)( (unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u );
}

/* menu/console taps: keydown now, keyup after a short delay (serviced in the loop) */
#define CTRL_TAPS   8
#define CTRL_TAP_MS 60
/* BACK is a confirm-to-quit: one tap arms + prompts, a second tap within this
 * window actually quits, so an accidental BACK can't dump you to the HMI. */
#define BACK_CONFIRM_MS 3000
static int          s_tap_key[CTRL_TAPS];
static unsigned int s_tap_at[CTRL_TAPS];
static void ctrl_tap_key( int key )
{
	int i;
	if ( !key ) return;
	for ( i = 0; i < CTRL_TAPS; i++ )
		if ( !s_tap_key[i] ) { s_tap_key[i] = key; s_tap_at[i] = ms_now() + CTRL_TAP_MS; push_event( IE_KEY, key, 1 ); return; }
}
static void ctrl_service_taps( unsigned int now )
{
	int i;
	for ( i = 0; i < CTRL_TAPS; i++ )
		if ( s_tap_key[i] && (int)( now - s_tap_at[i] ) >= 0 ) { push_event( IE_KEY, s_tap_key[i], 0 ); s_tap_key[i] = 0; }
}

static const char *s_dir_down_cmd[4] = { "+forward", "+back", "+moveleft", "+moveright" };
static const char *s_dir_up_cmd[4]   = { "-forward", "-back", "-moveleft", "-moveright" };

static void *ctrl_thread( void *arg )
{
	struct evo_feed *f = NULL;
	int fd = -1;
	unsigned int tail = 0, last_grab = 0, menu_down_ms = 0, back_arm_ms = 0;
	int dir_down[4] = { 0, 0, 0, 0 }, raw_dir = -1, fire_down = 0;
	const char *env = getenv( "EVO_Q3_TURN" );
	/* optional decode log for host-driven verification (EVO_Q3_CTRL_LOG=1) */
	FILE *lg = getenv( "EVO_Q3_CTRL_LOG" ) ? fopen( "/dev/shmem/evo_q3ctrl.log", "w" ) : NULL;
	(void)arg;
	if ( env && atoi( env ) > 0 ) s_turn_scale = atoi( env );

	while ( !s_shutdown ) {
		unsigned int now = ms_now();
		/* (re)attach to the feed ring; evo_ctrlfeed may start after us, or restart */
		if ( !f ) {
			if ( fd < 0 ) fd = open( EVO_FEED_PATH, O_RDWR );
			if ( fd >= 0 ) {
				void *m = mmap( NULL, sizeof( *f ), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
				if ( m != MAP_FAILED ) {
					f = (struct evo_feed *)m;
					if ( f->magic != EVO_FEED_MAGIC ) { munmap( m, sizeof( *f ) ); f = NULL; }
					else { tail = f->head;            /* only react to events from now on */
					       if ( lg ) { fprintf( lg, "feed attached head=%u turn_scale=%d\n", tail, s_turn_scale ); fflush( lg ); } }
				}
			}
			if ( !f ) { usleep( 300000 ); continue; } /* no ring yet: poll politely */
		}

		/* GRAB: own the controller while the game runs (HMI muted by the tee); refresh
		 * it so it lapses within the TTL if we die and the HMI gets the knob back. */
		if ( (int)( now - last_grab ) > 400 ) { f->grab_until = now + EVO_GRAB_TTL_MS; last_grab = now; }
		ctrl_service_taps( now );

		{
			unsigned int head = f->head;
			int ui = ( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CONSOLE ) ) ? 1 : 0;
			if ( (int)( head - tail ) < 0 ) tail = head;       /* producer re-init: resync */
			if ( head == tail ) { usleep( 5000 ); continue; }  /* ~5ms idle; keeps it snappy */
			while ( tail != head ) {
				struct evo_ev ev = f->ev[ tail % EVO_FEED_RING ];
				tail++;
				switch ( ev.kind ) {
				case EVO_K_ROTARY: {
					int d = (signed char)ev.b[0];
					if ( ui ) { int n = d < 0 ? -d : d; if ( n > 4 ) n = 4; while ( n-- ) ctrl_tap_key( d > 0 ? K_DOWNARROW : K_UPARROW ); }
					else if ( d ) push_event( IE_MOUSE, d * s_turn_scale, 0 );   /* + = clockwise = turn right */
					if ( lg ) { fprintf( lg, "ROTARY %+d -> %s %d\n", d, ui ? "menu-step" : "mouse-dx", ui ? d : d * s_turn_scale ); fflush( lg ); }
					break;
				}
				case EVO_K_DIR: {
					/* Decode from the RAW tilt code b[2] (bench-confirmed: 01 fwd, 04 back,
					 * 08 left, 02 right; release carries 00). Fall back to b[0] = EVO_DIR_*. */
					int down = ev.b[1], code = ev.b[2], dir;
					if ( down ) { dir = code == 0x01 ? EVO_DIR_FWD  : code == 0x04 ? EVO_DIR_BACK :
					                    code == 0x08 ? EVO_DIR_LEFT : code == 0x02 ? EVO_DIR_RIGHT : ev.b[0];
					              raw_dir = dir; }
					else { dir = raw_dir >= 0 ? raw_dir : ev.b[0]; raw_dir = -1; }
					if ( dir < 0 || dir > 3 ) break;
					if ( ui ) {
						if ( down ) ctrl_tap_key( dir == EVO_DIR_FWD ? K_UPARROW : dir == EVO_DIR_BACK ? K_DOWNARROW :
						                          dir == EVO_DIR_LEFT ? K_LEFTARROW : K_RIGHTARROW );
					} else {
						if ( down && !dir_down[dir] )      { push_cmd( s_dir_down_cmd[dir] ); dir_down[dir] = 1; }
						else if ( !down && dir_down[dir] ) { push_cmd( s_dir_up_cmd[dir] );   dir_down[dir] = 0; }
					}
					if ( lg ) { fprintf( lg, "DIR dir=%d %s%s -> %s\n", dir, down ? "down" : "up", ui ? " (menu)" : "",
					                     ui ? "arrow" : ( down ? s_dir_down_cmd[dir] : s_dir_up_cmd[dir] ) ); fflush( lg ); }
					break;
				}
				case EVO_K_CENTER:
					if ( ui ) { if ( ev.b[1] ) ctrl_tap_key( K_ENTER ); }
					else { push_cmd( ev.b[1] ? "+attack" : "-attack" ); fire_down = ev.b[1] ? 1 : 0; }
					if ( lg ) { fprintf( lg, "CENTER %s -> %s\n", ev.b[1] ? "down" : "up",
					                     ui ? "menu-enter" : ( ev.b[1] ? "+attack" : "-attack" ) ); fflush( lg ); }
					break;
				case EVO_K_BUTTON:
				default: {
					int down = ev.b[1];
					switch ( ev.b[0] ) {
					case 0x02:  /* BACK = confirm-to-quit. First tap arms + shows an on-screen
					             * prompt (console notify area); a second tap within
					             * BACK_CONFIRM_MS quits to the launcher/HMI. Prevents an
					             * accidental BACK from dumping you out mid-game. */
						if ( down ) {
							unsigned int bn = ms_now();
							if ( back_arm_ms && (int)( bn - back_arm_ms ) <= BACK_CONFIRM_MS ) {
								back_arm_ms = 0;
								f->grab_until = 0;
								push_cmd( "quit" );
							} else {
								back_arm_ms = bn | 1u;
								push_cmd( "echo \"^3Leaving Quake 3 - press BACK again to quit\"" );
							}
						}
						break;
					case 0x01:  /* MENU short tap = ESC (menu). Long hold is reserved for the
					             * dual-screen Android video-switch gesture, so we ignore it. */
						if ( down ) menu_down_ms = ms_now() | 1u;
						else if ( menu_down_ms ) { if ( (int)( ms_now() - menu_down_ms ) < 600 ) ctrl_tap_key( K_ESCAPE ); menu_down_ms = 0; }
						break;
					case 0x10: if ( down && !ui ) push_cmd( "weapnext" ); break;              /* NAV  = next weapon */
					case 0x20: if ( down && !ui ) push_cmd( "weapprev" ); break;              /* TEL  = prev weapon */
					case 0x08: if ( !ui ) push_cmd( down ? "+moveup" : "-moveup" ); break;    /* MEDIA = jump */
					case 0x04: if ( !ui ) push_cmd( down ? "+scores" : "-scores" ); break;    /* OPTION = scoreboard */
					case 0x40: if ( !ui ) push_cmd( down ? "+zoom" : "-zoom" ); break;        /* COM = zoom */
					}
					if ( lg ) { fprintf( lg, "BTN id=0x%02x %s\n", ev.b[0], down ? "down" : "up" ); fflush( lg ); }
					break;
				}
				}
			}
		}
	}

	/* release anything still held + hand the controller back to the HMI on exit */
	{ int d; for ( d = 0; d < 4; d++ ) if ( dir_down[d] ) push_cmd( s_dir_up_cmd[d] ); }
	if ( fire_down ) push_cmd( "-attack" );
	if ( f ) { f->grab_until = 0; munmap( (void *)f, sizeof( *f ) ); }
	if ( fd >= 0 ) close( fd );
	if ( lg ) fclose( lg );
	return NULL;
}

/* ===============  pump the ring into the engine (main thread)  ============= */
void HandleEvents( void )
{
	for ( ;; ) {
		inEvent_t e;
		pthread_mutex_lock( &s_lock );
		if ( s_head == s_tail ) { pthread_mutex_unlock( &s_lock ); break; }
		e = s_ring[ s_tail % INEV_RING ];
		s_tail++;
		pthread_mutex_unlock( &s_lock );

		switch ( e.type ) {
			case IE_KEY:   Sys_QueEvent( 0, SE_KEY,   e.v1, e.v2, 0, NULL ); break;
			case IE_CHAR:  Sys_QueEvent( 0, SE_CHAR,  e.v1, 0,    0, NULL ); break;
			case IE_MOUSE: Sys_QueEvent( 0, SE_MOUSE, e.v1, e.v2, 0, NULL ); break;
			case IE_CONSOLE: {                     /* SE_CONSOLE owns a Z_Malloc'd buffer (engine Z_Free's it) */
				int len = (int)strlen( e.cmd ) + 1;
				char *b = Z_Malloc( len );
				memcpy( b, e.cmd, len );
				Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
				break;
			}
		}
	}
}

void IN_Frame( void )
{
	HandleEvents();
}

void IN_Init( void )
{
	Com_Printf( "QNX IN_Init: keyboard %s, mouse %s\n", QNX_KBD_DEV, QNX_MOUSE_DEV );
	Key_ClearStates();

	s_shutdown = 0;
	s_head = s_tail = 0;
	s_shift = 0;

	s_kbd_fd   = open( QNX_KBD_DEV, O_RDONLY );
	s_mouse_fd = open( QNX_MOUSE_DEV, O_RDONLY );
	if ( s_kbd_fd < 0 )   Com_Printf( "QNX IN: keyboard not open yet (%s) - will retry\n", strerror( errno ) );
	if ( s_mouse_fd < 0 ) Com_Printf( "QNX IN: mouse not open yet (%s) - will retry\n", strerror( errno ) );

	if ( pthread_create( &s_kbd_tid, NULL, kbd_thread, NULL ) == 0 )   s_kbd_running = 1;
	else Com_Printf( "QNX IN: failed to start keyboard thread\n" );
	if ( pthread_create( &s_mouse_tid, NULL, mouse_thread, NULL ) == 0 ) s_mouse_running = 1;
	else Com_Printf( "QNX IN: failed to start mouse thread\n" );
	if ( pthread_create( &s_ctrl_tid, NULL, ctrl_thread, NULL ) == 0 )  s_ctrl_running = 1;
	else Com_Printf( "QNX IN: failed to start iDrive controller thread\n" );
}

static void stop_thread( pthread_t tid )
{
	struct timespec dl;
	pthread_cancel( tid );              /* read() is a cancellation point; devu-* honour the unblock */
	clock_gettime( CLOCK_REALTIME, &dl );
	dl.tv_sec += 2;
	if ( pthread_timedjoin( tid, NULL, &dl ) != 0 )
		pthread_detach( tid );          /* bounded_read makes this rare; detach as a safety net */
}

void IN_Shutdown( void )
{
	s_shutdown = 1;
	if ( s_kbd_running )   { stop_thread( s_kbd_tid );   s_kbd_running = 0; }
	if ( s_mouse_running ) { stop_thread( s_mouse_tid ); s_mouse_running = 0; }
	if ( s_ctrl_running )  { stop_thread( s_ctrl_tid );  s_ctrl_running = 0; }
	if ( s_kbd_fd >= 0 )   { close( s_kbd_fd );   s_kbd_fd = -1; }
	if ( s_mouse_fd >= 0 ) { close( s_mouse_fd ); s_mouse_fd = -1; }
}

void IN_Restart( void )
{
	IN_Shutdown();
	IN_Init();
}

/* no keyboard LED state source; shift/caps handled internally for SE_CHAR */
qboolean Key_CapsLockOn( void )
{
	return qfalse;
}

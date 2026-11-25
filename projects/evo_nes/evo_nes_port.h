/* evo_nes_port.h - the seam between the launcher-side frontend (evo_nes.c) and
 * the Nofrendo O/S port (evo_nes_osd.c).
 *
 * The two halves never include each other's headers: evo_nes.c owns the QNX
 * Screen/ILM graphics, iDrive feed ring, USB keyboard and QSA audio; evo_nes_osd.c
 * owns everything that includes nofrendo's headers (whose noftypes.h bool/NULL
 * macros do not mix well with the system headers). Plain C types only here.
 */
#ifndef EVO_NES_PORT_H
#define EVO_NES_PORT_H

/* NES joypad-1 bits, in the order osd_getinput() maps them to nofrendo events. */
#define NESBTN_A      0x01u
#define NESBTN_B      0x02u
#define NESBTN_SELECT 0x04u
#define NESBTN_START  0x08u
#define NESBTN_UP     0x10u
#define NESBTN_DOWN   0x20u
#define NESBTN_LEFT   0x40u
#define NESBTN_RIGHT  0x80u

/* ---- services the osd port calls, implemented by the frontend (evo_nes.c) ---- */
/* Present one emulated frame: `h` rows of `w` 8-bit palette indices, with a
 * 256-entry 0xAARRGGBB palette. Scales, centres and presents. */
void     nesfe_blit(const unsigned char *const *lines, int w, int h, const unsigned int *pal);
/* Pull one frame of 16-bit mono APU samples through `pull` and play them. The
 * QSA write blocks, which paces emulation to the audio clock. 0 = no audio device. */
int      nesfe_audio_frame(void (*pull)(void *buf, int nsamples));
int      nesfe_audio_rate(void);        /* device sample rate, 0 = running silent */
unsigned nesfe_buttons(void);           /* joypad-1 NESBTN_* mask currently held */
unsigned nesfe_buttons2(void);          /* joypad-2 mask (a second gamepad), 0 if none */
int      nesfe_quit_requested(void);    /* BACK confirmed, Esc confirmed, or a signal */
void     nesfe_log(const char *s);      /* nofrendo's log output */

/* ---- entry points the frontend calls, implemented by the osd port ---- */
int      nesosd_run(const char *rompath);   /* run nofrendo on this ROM; returns on quit */
void     nesosd_shutdown(void);             /* stop the frame-clock thread, free video */

#endif /* EVO_NES_PORT_H */

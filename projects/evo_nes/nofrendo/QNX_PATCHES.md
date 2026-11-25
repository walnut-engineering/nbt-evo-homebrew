# QNX changes to the vendored Nofrendo core

Kept deliberately tiny so the core stays recognisably upstream. Everything
platform-specific lives outside this directory in `../evo_nes_osd.c`.

1. **`log.c` - `log_assert()`**: the mirror we vendored carried the ESP32 (Xtensa)
   breakpoint `asm("break.n 1")`, which is not an ARM instruction (the SDP
   assembler rejects it). Replaced with a portable `abort()`.

2. **`nes/nes.c` - `nes_emulate()`**: when no frame is due (waiting for the next
   60 Hz tick) the original loop spins hot. Added a 1 ms `usleep()` in that idle
   branch so a waiting emulator doesn't pin a core.

3. **`nes/nes_rom.c` - `rom_free()`**: this mirror loads the cartridge from memory
   (`rom_load()` calls `osd_getromdata()` and points `rominfo->rom` / `->vrom`
   *into* that image instead of copying), but `rom_free()` still `free()`d those
   interior pointers - harmless on ESP32 flash, heap corruption on QNX at quit.
   Removed the two frees; the image buffer is owned and freed by the O/S layer
   (`evo_nes_osd.c: osd_getromdata()` / `nesosd_shutdown()`).

4. **`cpu/nes6502.c` - `zp_readword()` / `bank_readword()`**: the little-endian fast
   path read 16-bit operands with `*(uint16 *)` at arbitrary (odd) addresses. This
   unit enforces strict ARM alignment (SCTLR.A), so that is a SIGBUS the moment the
   CPU fetches its first absolute-address operand. Both now read two bytes.

5. **`nofrendo.c` - `internal_insert()`**: the frame buffer was created 224 lines tall
   (`NES_VISIBLE_HEIGHT`), but this mirror's PPU renders straight into it for all
   240 scanlines, so scanline 224 indexed past the bitmap's `line[]` array (NULL
   line pointer -> `memset` SIGSEGV on the first frame). Now `NES_SCREEN_HEIGHT`;
   the frontend crops the 8-line overscan when presenting.

6. **`vid_drv.c` - `vid_setmode()`**: the primary buffer is created with the 8-pixel
   overdraw margin the original nofrendo gave its render buffer (the scroll code
   writes a partial tile before x=0); the mirror had set it to 0.

7. **`log.c`**: the real `log_init/log_print/log_printf` (which forward to the
   function registered with `log_chain_logfunc`) were compiled only under
   `NOFRENDO_DEBUG`, otherwise stubs - so nofrendo's own messages ("ROM loaded",
   "video driver", mapper errors) never reached our log. That guard is now always
   on; `NOFRENDO_DEBUG` itself (memguard tracking, asserts) stays off.

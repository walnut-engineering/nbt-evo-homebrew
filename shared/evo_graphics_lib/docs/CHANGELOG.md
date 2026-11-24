
## 2026-11-06 — double-buffered presentation (flash fix)
`src/evo_display_unified.c` now creates **two** Screen window buffers and hands applications a **stable
shadow frame** (malloc'd, persists across presents). `evo_context_present()` copies the completed shadow
frame into the scan-out buffer that is *not* on screen and posts that one. Previously a single buffer was
drawn in place while the compositor read it, so full-frame redraws (terminal; cursor at high motion rates)
visibly flashed. Cost: one width×height×4 memcpy per present (~3 MB at 1440×540). Applications are
unchanged — `evo_context_get_framebuffer()` still returns one persistent pointer. Verified on the bench with
evo_cursor ("looks and works great"). DOOM must be rebuilt to pick it up; evo_terminal and evo_cursor were.

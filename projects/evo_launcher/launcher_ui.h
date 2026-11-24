/* launcher_ui.h - look-and-feel layer of the homebrew launcher.
 *
 * Draws the launcher page and the "starting app" splash into a 1440x540
 * 0xAARRGGBB framebuffer. It has no QNX or graphics-lib dependency, so the same
 * code renders on the head unit and in the host preview tool (tools/ui_preview.c).
 *
 * The look adapts to the head unit it runs on: the HMI generation, ID version
 * and brand are detected at start-up, and fonts, tile artwork and the accent
 * colour are loaded from the unit's own HMI resources. Nothing from the HMI is
 * shipped with the launcher; when a resource is missing the matching element is
 * drawn procedurally. See launcher_sdk/README.md, "Look and theme".
 */
#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include <stdint.h>

#define LUI_W 1440
#define LUI_H 540

/* One launcher entry. Name and icon pixels stay owned by the caller and must
 * outlive the next lui_set_items() call. icon_px is 0xAARRGGBB, straight alpha,
 * or NULL for a placeholder. */
struct lui_item {
    const char     *name;
    int             icon_w, icon_h;
    const uint32_t *icon_px;
};

typedef struct lui lui_t;

/* override: NULL or "auto" = detect; "id5" = glossy main-menu tiles from the
 * HMI assets; "flat" = procedural tiles (the fallback look). */
lui_t      *lui_create(const char *override);
void        lui_destroy(lui_t *u);

/* One line describing what was detected and loaded, for the log. */
const char *lui_theme_desc(const lui_t *u);

void        lui_set_items(lui_t *u, const struct lui_item *items, int n);

/* Number of entries shown side by side (the caller does not need it for
 * navigation; selection is a plain index). */
int         lui_visible_count(const lui_t *u);

/* Draw the page. sel = selected entry, now_ms = monotonic time (drives the
 * selection glow), alpha = 0..255 whole-page opacity for fade in / fade out. */
void        lui_render_page(lui_t *u, uint32_t *fb, int sel, unsigned now_ms, unsigned alpha);

/* Draw the opaque "starting <app>" screen; t_ms = time since the launch. */
void        lui_render_splash(lui_t *u, uint32_t *fb, const struct lui_item *it, unsigned t_ms);

#endif

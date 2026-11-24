/*
 * EVO Text Advanced - Dynamic Font System Integration
 * 
 * This module provides enhanced text rendering with dynamic system font loading,
 * deprecating the old bitmap font system in favor of FreeType2-based fonts.
 * 
 * Key features:
 * - Dynamic loading of system fonts from /usr/share/fonts/
 * - FreeType2 integration for scalable fonts
 * - Backward compatibility with existing evo_text API
 * - Font caching and performance optimization
 * - Automatic fallback to bitmap fonts if FreeType2 fails
 */

#ifndef EVO_TEXT_ADVANCED_H
#define EVO_TEXT_ADVANCED_H

#include "evo_text.h"
#include "evo_fonts.h"
#include "evo_graphics.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enhanced font system configuration */
#define EVO_TEXT_ADVANCED_CACHE_SIZE 256
#define EVO_TEXT_ADVANCED_MAX_SIZE   72
#define EVO_TEXT_ADVANCED_MIN_SIZE   8

/* Font preference system */
typedef enum {
    EVO_TEXT_PREFER_SYSTEM_FONTS = 0,  /* Try system fonts first */
    EVO_TEXT_PREFER_BITMAP_FONTS,      /* Use bitmap fonts (legacy) */
    EVO_TEXT_FORCE_SYSTEM_FONTS,       /* Only use system fonts */
    EVO_TEXT_FORCE_BITMAP_FONTS        /* Only use bitmap fonts */
} evo_text_font_preference_t;

/* Enhanced font size with pixel precision */
typedef struct {
    int size_pt;                        /* Point size for scalable fonts */
    evo_font_size_t legacy_size;        /* Fallback bitmap size */
    int pixel_height;                   /* Actual pixel height */
} evo_text_advanced_size_t;

/* Text rendering quality settings */
typedef enum {
    EVO_TEXT_QUALITY_FAST = 0,          /* Fast rendering, minimal quality */
    EVO_TEXT_QUALITY_NORMAL,            /* Balanced quality and performance */
    EVO_TEXT_QUALITY_HIGH,              /* High quality, slower rendering */
    EVO_TEXT_QUALITY_SUBPIXEL           /* Subpixel rendering (if supported) */
} evo_text_quality_t;

/* Text rendering context */
typedef struct {
    evo_font_manager_t *font_manager;
    evo_text_font_preference_t preference;
    evo_text_quality_t quality;
    const evo_font_info_t *current_font;
    evo_font_handle_t *current_handle;
    evo_text_advanced_size_t current_size;
    int kerning_enabled;
    int hinting_enabled;
    uint32_t text_color;
    uint32_t background_color;
    int background_enabled;
} evo_text_advanced_context_t;

/* Advanced text measurement result */
typedef struct {
    int width;                          /* Total text width */
    int height;                         /* Total text height */
    int advance_x;                      /* X advance for cursor positioning */
    int advance_y;                      /* Y advance for line positioning */
    int baseline_offset;                /* Offset from top to baseline */
} evo_text_measurement_t;

/* ============================================================================
 * INITIALIZATION AND CONFIGURATION
 * ============================================================================ */

/**
 * Initialize the advanced text system
 * @return 0 on success, negative on error
 */
int evo_text_advanced_init(void);

/**
 * Shutdown the advanced text system
 */
void evo_text_advanced_shutdown(void);

/**
 * Check if advanced text system is available
 * @return 1 if available, 0 if not
 */
int evo_text_advanced_is_available(void);

/**
 * Set font preference for text rendering
 * @param preference Font preference setting
 * @return 0 on success, negative on error
 */
int evo_text_advanced_set_preference(evo_text_font_preference_t preference);

/**
 * Set text rendering quality
 * @param quality Rendering quality setting
 * @return 0 on success, negative on error
 */
int evo_text_advanced_set_quality(evo_text_quality_t quality);

/* ============================================================================
 * FONT MANAGEMENT
 * ============================================================================ */

/**
 * Load a system font by family name
 * @param family_name Font family name (e.g., "monospace", "sans-serif")
 * @param size_pt Font size in points
 * @return Font handle on success, NULL on failure
 */
evo_font_handle_t* evo_text_advanced_load_font(const char *family_name, int size_pt);

/**
 * Load best available font by type
 * @param type Font type preference
 * @param size_pt Font size in points
 * @return Font handle on success, NULL on failure
 */
evo_font_handle_t* evo_text_advanced_load_font_by_type(evo_font_type_t type, int size_pt);

/**
 * Get default font for text rendering
 * @param size_pt Font size in points
 * @return Font handle on success, NULL on failure
 */
evo_font_handle_t* evo_text_advanced_get_default_font(int size_pt);

/**
 * Release a font handle
 * @param handle Font handle to release
 */
void evo_text_advanced_release_font(evo_font_handle_t *handle);

/* ============================================================================
 * TEXT MEASUREMENT
 * ============================================================================ */

/**
 * Measure text with advanced metrics
 * @param font Font handle
 * @param text Text to measure
 * @param measurement Output measurement result
 * @return 0 on success, negative on error
 */
int evo_text_advanced_measure(evo_font_handle_t *font, const char *text,
                             evo_text_measurement_t *measurement);

/**
 * Measure single character
 * @param font Font handle
 * @param codepoint Unicode codepoint
 * @param measurement Output measurement result
 * @return 0 on success, negative on error
 */
int evo_text_advanced_measure_char(evo_font_handle_t *font, uint32_t codepoint,
                                  evo_text_measurement_t *measurement);

/* ============================================================================
 * TEXT RENDERING
 * ============================================================================ */

/**
 * Draw text with advanced font system
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param font Font handle (NULL for default)
 * @param color Text color
 * @return Width of drawn text, negative on error
 */
int evo_text_advanced_draw(evo_graphics_context_t *ctx, int x, int y,
                          const char *text, evo_font_handle_t *font, uint32_t color);

/**
 * Draw text with background
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param font Font handle (NULL for default)
 * @param text_color Text color
 * @param bg_color Background color
 * @return Width of drawn text, negative on error
 */
int evo_text_advanced_draw_with_background(evo_graphics_context_t *ctx, int x, int y,
                                          const char *text, evo_font_handle_t *font,
                                          uint32_t text_color, uint32_t bg_color);

/* ============================================================================
 * COMPATIBILITY LAYER
 * ============================================================================ */

/**
 * Enhanced version of evo_text_draw_text using system fonts
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param size Font size category (converted to points)
 * @param color Text color
 * @return Width of drawn text, negative on error
 */
int evo_text_enhanced_draw_text(evo_graphics_context_t *ctx, int x, int y,
                               const char *text, evo_font_size_t size, uint32_t color);

/**
 * Get enhanced text width (system fonts)
 * @param text Text to measure
 * @param size Font size category
 * @return Text width in pixels
 */
int evo_text_enhanced_get_text_width(const char *text, evo_font_size_t size);

/* ============================================================================
 * FONT DISCOVERY AND DIAGNOSTICS
 * ============================================================================ */

/**
 * Discover and list available system fonts
 * @return Number of fonts discovered
 */
int evo_text_advanced_discover_fonts(void);

/**
 * Print system font information
 */
void evo_text_advanced_print_font_info(void);

/**
 * Test font loading capability
 * @return 0 if fonts can be loaded, negative on error
 */
int evo_text_advanced_test_font_loading(void);

/**
 * Get current font system status
 * @param system_fonts_available Output: system fonts available
 * @param bitmap_fallback_active Output: bitmap fallback active
 * @return Current preference setting
 */
evo_text_font_preference_t evo_text_advanced_get_status(int *system_fonts_available,
                                                       int *bitmap_fallback_active);

#ifdef __cplusplus
}
#endif

#endif /* EVO_TEXT_ADVANCED_H */

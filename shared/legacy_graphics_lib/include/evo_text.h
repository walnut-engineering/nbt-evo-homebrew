/*
 * EVO Text Rendering Library Header - Optimized Single Font
 * 
 * Optimized bitmap font rendering system for EVO embedded systems.
 * Uses a single scalable 8x12 font for all sizes, perfect for 1440x540 displays.
 * Memory efficient with excellent scaling characteristics.
 */

#ifndef EVO_TEXT_H
#define EVO_TEXT_H

#include <stdint.h>
#include <stddef.h>

/* Include graphics context type */
#include "evo_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Font size enumeration - using clean integer scaling for crisp bitmap fonts */
typedef enum {
    EVO_FONT_SMALL = 0,    /* 8x12 pixels (1x scale) */
    EVO_FONT_MEDIUM,       /* 16x24 pixels (2x scale) */
    EVO_FONT_LARGE,        /* 24x36 pixels (3x scale) */
    EVO_FONT_XLARGE,       /* 24x36 pixels (3x scale) */
    EVO_FONT_XXLARGE,      /* 32x48 pixels (4x scale) */
    EVO_FONT_COUNT
} evo_font_size_t;

/* Text alignment options */
typedef enum {
    EVO_TEXT_ALIGN_LEFT = 0,
    EVO_TEXT_ALIGN_CENTER,
    EVO_TEXT_ALIGN_RIGHT
} evo_text_align_t;

/* Font dimension structure */
typedef struct {
    int width;
    int height;
} evo_font_dimensions_t;

/* ============================================================================
 * CORE TEXT RENDERING FUNCTIONS
 * ============================================================================ */

/**
 * Get font dimensions for a given font size
 * @param font_size Font size to query
 * @return Font dimensions structure
 */
evo_font_dimensions_t evo_text_get_font_dimensions(evo_font_size_t font_size);

/**
 * Draw a single character
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param character Character to draw
 * @param font_size Font size
 * @param color Color
 * @return Character width in pixels
 */
int evo_text_draw_char(evo_graphics_context_t *ctx, int x, int y, 
                       char character, evo_font_size_t font_size, uint32_t color);

/**
 * Draw text string
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param font_size Font size
 * @param color Text color
 * @return Total width drawn in pixels
 */
int evo_text_draw_text(evo_graphics_context_t *ctx, int x, int y, 
                       const char *text, evo_font_size_t font_size, uint32_t color);

/**
 * Draw text with background
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param font_size Font size
 * @param text_color Text color
 * @param bg_color Background color
 * @return Total width drawn in pixels
 */
int evo_text_draw_text_with_background(evo_graphics_context_t *ctx, int x, int y, 
                                       const char *text, evo_font_size_t font_size, 
                                       uint32_t text_color, uint32_t bg_color);

/**
 * Draw aligned text within a specified width
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text to draw
 * @param font_size Font size
 * @param color Text color
 * @param alignment Text alignment
 * @param max_width Maximum width for alignment
 * @return Total width drawn in pixels
 */
int evo_text_draw_text_aligned(evo_graphics_context_t *ctx, int x, int y, 
                               const char *text, evo_font_size_t font_size,
                               uint32_t color, evo_text_align_t alignment, int max_width);

/**
 * Draw formatted text (printf-style)
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param font_size Font size
 * @param color Text color
 * @param format Format string
 * @param ... Arguments
 * @return Total width drawn in pixels
 */
int evo_text_draw_text_formatted(evo_graphics_context_t *ctx, int x, int y, 
                                 evo_font_size_t font_size, uint32_t color, 
                                 const char *format, ...);

/**
 * Draw progress bar text (centered within bar)
 * @param ctx Graphics context
 * @param bar_x Progress bar X coordinate
 * @param bar_y Progress bar Y coordinate  
 * @param bar_width Progress bar width
 * @param bar_height Progress bar height
 * @param text Text to draw
 * @param font_size Font size
 * @param color Text color
 * @return Total width drawn in pixels
 */
int evo_text_draw_progress_text(evo_graphics_context_t *ctx, int bar_x, int bar_y,
                                int bar_width, int bar_height, const char *text,
                                evo_font_size_t font_size, uint32_t color);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Check if a character is supported in the font set
 * @param character Character to check
 * @return 1 if supported, 0 if not
 */
int evo_text_is_char_supported(char character);

/**
 * Calculate text width for a given string and font size
 * @param text Text string
 * @param font_size Font size
 * @return Text width in pixels
 */
int evo_text_get_text_width(const char *text, evo_font_size_t font_size);

/* ============================================================================
 * SPECIAL CHARACTER CONSTANTS
 * ============================================================================ */

/* Degree symbol - ASCII 127 in our extended character set */
#define EVO_TEXT_DEGREE_SYMBOL '\x7F'

#ifdef __cplusplus
}
#endif

#endif /* EVO_TEXT_H */

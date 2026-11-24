/*
 * EVO Graphics Library - 2D Drawing and Text Rendering
 * 
 * This library provides efficient 2D graphics drawing and text rendering
 * specifically for EVO embedded systems. It works with EVO Screen buffers
 * and provides optimized drawing primitives.
 * 
 * Design goals:
 * - Fast direct framebuffer drawing
 * - Minimal memory footprint for embedded systems
 * - Simple bitmap font system for text rendering
 * - No external dependencies beyond EVO Screen
 * - Optimized for automotive/infotainment use cases
 * 
 * Usage:
 * 1. Use with evo_display_helper for Screen buffer management
 * 2. Call drawing functions with buffer pointer and dimensions
 * 3. Built-in fonts for common text rendering needs
 * 
 * Build: Include this header and link evo_graphics.c
 */

#ifndef EVO_GRAPHICS_H
#define EVO_GRAPHICS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * COLOR FORMAT CONVERSION
 * ============================================================================ */

/**
 * Color format conversion - RGBA8888 format (QNX system standard)
 * 
 * QNX SYSTEM DEFINITION (from img/img.h and screen/screen.h):
 * - Format: IMG_FMT_RGBA8888 = IMG_FMT_PKLE_ABGR8888 (packed little-endian)
 * - Screen API: SCREEN_FORMAT_RGBA8888 = "32 bits per pixel RGB with alpha channel"
 * - Memory layout: Little-endian ABGR byte order [A, B, G, R] in memory
 * - As uint32_t: 0xAABBGGRR on little-endian systems
 * 
 * API CONVENTION:
 * - RGBA8888 refers to the API parameter order (Red, Green, Blue, Alpha)
 * - Memory storage follows QNX IMG_FMT_PKLE_ABGR8888 specification
 * - Color macro: ((a) << 24) | ((r) << 16) | ((g) << 8) | (b) = 0xAABBGGRR
 * 
 * ENDIANNESS TEST RESULTS (2025-06-28):
 * - Tested 4 different byte orders on actual EVO hardware
 * - RGBA API with ABGR memory layout showed correct colors
 * - Winning format: RED=0xFFFF0000, GREEN=0xFF00FF00, BLUE=0xFF0000FF, YELLOW=0xFFFFFF00
 */
static inline uint32_t evo_graphics_convert_color(uint32_t rgba_color) {
    /* Hardware expects RGBA8888 format (QNX IMG_FMT_PKLE_ABGR8888) - no conversion needed */
    return rgba_color;
}

/**
 * Color creation macros for RGBA8888 format (QNX system standard)
 * API uses RGBA parameter order for intuitive color creation
 * Memory layout: RGBA8888 (QNX IMG_FMT_PKLE_ABGR8888 - packed little-endian ABGR)
 * Example: EVO_COLOR_RGBA(255, 0, 0, 255) creates red color = 0xFFFF0000
 */
#define EVO_COLOR_RGBA(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define EVO_COLOR_RGB(r, g, b) EVO_COLOR_RGBA(r, g, b, 255)

/* ============================================================================
 * GRAPHICS CONTEXT AND TYPES
 * ============================================================================ */

/**
 * Graphics drawing context - wraps a framebuffer for drawing operations
 */
typedef struct {
    uint32_t *buffer;       /* Framebuffer pointer (RGBA8888 format) */
    int width;              /* Buffer width in pixels */
    int height;             /* Buffer height in pixels */
    int stride;             /* Buffer stride in bytes */
    uint32_t *pixels;       /* Convenience pixel array pointer */
} evo_graphics_context_t;

/* Text rendering is now handled by evo_text.h library */

/* ============================================================================
 * COLOR DEFINITIONS
 * ============================================================================ */

/* Standard colors (RGBA8888 format) */
#define EVO_COLOR_BLACK          EVO_COLOR_RGBA(0, 0, 0, 255)
#define EVO_COLOR_WHITE          EVO_COLOR_RGBA(255, 255, 255, 255)
#define EVO_COLOR_RED            EVO_COLOR_RGBA(255, 0, 0, 255)
#define EVO_COLOR_GREEN          EVO_COLOR_RGBA(0, 255, 0, 255)
#define EVO_COLOR_BLUE           EVO_COLOR_RGBA(0, 0, 255, 255)
#define EVO_COLOR_YELLOW         EVO_COLOR_RGBA(255, 255, 0, 255)
#define EVO_COLOR_CYAN           EVO_COLOR_RGBA(0, 255, 255, 255)
#define EVO_COLOR_MAGENTA        EVO_COLOR_RGBA(255, 0, 255, 255)
#define EVO_COLOR_ORANGE         EVO_COLOR_RGBA(255, 128, 0, 255)
#define EVO_COLOR_PURPLE         EVO_COLOR_RGBA(128, 0, 255, 255)
#define EVO_COLOR_GRAY           EVO_COLOR_RGBA(128, 128, 128, 255)
#define EVO_COLOR_LIGHT_GRAY     EVO_COLOR_RGBA(192, 192, 192, 255)
#define EVO_COLOR_DARK_GRAY      EVO_COLOR_RGBA(64, 64, 64, 255)
#define EVO_COLOR_TRANSPARENT    EVO_COLOR_RGBA(0, 0, 0, 0)
#define EVO_COLOR_DARK_BLUE      EVO_COLOR_RGBA(0, 0, 102, 255)

/* Semi-transparent colors for overlays (50% alpha) */
#define EVO_COLOR_BLACK_SEMI     EVO_COLOR_RGBA(0, 0, 0, 128)
#define EVO_COLOR_WHITE_SEMI     EVO_COLOR_RGBA(255, 255, 255, 128)
#define EVO_COLOR_RED_SEMI       EVO_COLOR_RGBA(255, 0, 0, 128)
#define EVO_COLOR_GREEN_SEMI     EVO_COLOR_RGBA(0, 255, 0, 128)
#define EVO_COLOR_BLUE_SEMI      EVO_COLOR_RGBA(0, 0, 255, 128)
#define EVO_COLOR_YELLOW_SEMI    EVO_COLOR_RGBA(255, 255, 0, 128)
#define EVO_COLOR_CYAN_SEMI      EVO_COLOR_RGBA(0, 255, 255, 128)
#define EVO_COLOR_MAGENTA_SEMI   EVO_COLOR_RGBA(255, 0, 255, 128)

/* Light transparency (25% alpha) */
#define EVO_COLOR_BLACK_LIGHT    EVO_COLOR_RGBA(0, 0, 0, 64)
#define EVO_COLOR_WHITE_LIGHT    EVO_COLOR_RGBA(255, 255, 255, 64)
#define EVO_COLOR_BLUE_LIGHT     EVO_COLOR_RGBA(0, 0, 255, 64)
#define EVO_COLOR_RED_LIGHT      EVO_COLOR_RGBA(255, 0, 0, 64)

/* Heavy transparency (75% alpha) */
#define EVO_COLOR_BLACK_HEAVY    EVO_COLOR_RGBA(0, 0, 0, 192)
#define EVO_COLOR_WHITE_HEAVY    EVO_COLOR_RGBA(255, 255, 255, 192)
#define EVO_COLOR_BLUE_HEAVY     EVO_COLOR_RGBA(0, 0, 255, 192)

/* Automotive/UI specific colors */
#define EVO_COLOR_UI_BACKGROUND  EVO_COLOR_RGBA(26, 26, 26, 255)
#define EVO_COLOR_UI_ACCENT      EVO_COLOR_RGBA(0, 102, 204, 255)
#define EVO_COLOR_UI_SUCCESS     EVO_COLOR_RGBA(0, 204, 102, 255)
#define EVO_COLOR_UI_WARNING     EVO_COLOR_RGBA(255, 153, 0, 255)
#define EVO_COLOR_UI_ERROR       EVO_COLOR_RGBA(204, 0, 0, 255)
#define EVO_COLOR_UI_INFO        EVO_COLOR_RGBA(102, 153, 255, 255)

/* Alpha manipulation macros - ARGB8888 format */
#define EVO_MAKE_ALPHA(color, alpha) (((color) & 0x00FFFFFF) | (((alpha) & 0xFF) << 24))
#define EVO_GET_ALPHA(color) (((color) >> 24) & 0xFF)
#define EVO_SET_ALPHA(color, alpha) EVO_MAKE_ALPHA(color, alpha)

/* Color component extraction macros - ARGB8888 format (hardware-verified) */
#define EVO_GET_ALPHA(color) (((color) >> 24) & 0xFF)  /* Alpha is in bits 24-31 for ARGB8888 */
#define EVO_GET_RED(color)   (((color) >> 16) & 0xFF)  /* Red is in bits 16-23 for ARGB8888 */
#define EVO_GET_GREEN(color) (((color) >> 8) & 0xFF)   /* Green is in bits 8-15 for ARGB8888 */
#define EVO_GET_BLUE(color)  ((color) & 0xFF)          /* Blue is in bits 0-7 for ARGB8888 */

/* Color creation macro - ARGB format (hardware-verified) */
#define EVO_MAKE_COLOR(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* Common alpha values */
#define EVO_ALPHA_TRANSPARENT   0x00
#define EVO_ALPHA_LIGHT         0x40  /* 25% */
#define EVO_ALPHA_SEMI          0x80  /* 50% */
#define EVO_ALPHA_HEAVY         0xC0  /* 75% */
#define EVO_ALPHA_OPAQUE        0xFF  /* 100% */

/* ============================================================================
 * CORE GRAPHICS FUNCTIONS
 * ============================================================================ */

/**
 * Initialize graphics context from a framebuffer
 * 
 * @param buffer Framebuffer pointer (RGBA8888)
 * @param width Buffer width in pixels
 * @param height Buffer height in pixels  
 * @param stride Buffer stride in bytes
 * @return Graphics context or NULL on failure
 */
evo_graphics_context_t* evo_graphics_create_context(uint32_t *buffer, int width, 
                                                   int height, int stride);

/**
 * Destroy graphics context
 * 
 * @param ctx Graphics context to destroy
 */
void evo_graphics_destroy_context(evo_graphics_context_t **ctx);

/**
 * Change the target buffer for drawing operations.
 * This is useful when working with double-buffered displays.
 *
 * @param ctx Graphics context to modify
 * @param buffer New buffer to draw into
 */
void evo_graphics_set_buffer(evo_graphics_context_t *ctx, uint32_t *buffer);

/**
 * Clear entire buffer with specified color
 * 
 * @param ctx Graphics context
 * @param color Fill color (RGBA8888)
 */
void evo_graphics_clear(evo_graphics_context_t *ctx, uint32_t color);

/**
 * Set a single pixel
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color (RGBA8888)
 */
void evo_graphics_set_pixel(evo_graphics_context_t *ctx, int x, int y, uint32_t color);

/**
 * Get a single pixel color
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @return Pixel color (RGBA8888)
 */
uint32_t evo_graphics_get_pixel(evo_graphics_context_t *ctx, int x, int y);

/* ============================================================================
 * PRIMITIVE DRAWING FUNCTIONS
 * ============================================================================ */

/**
 * Draw a filled rectangle
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Fill color (RGBA8888)
 */
void evo_graphics_draw_rectangle(evo_graphics_context_t *ctx, int x, int y, 
                               int width, int height, uint32_t color);

/**
 * Draw a rectangle outline
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Outline color (RGBA8888)
 */
void evo_graphics_draw_rectangle_outline(evo_graphics_context_t *ctx, int x, int y, 
                                       int width, int height, uint32_t color);

/**
 * Draw a line using Bresenham's algorithm
 * 
 * @param ctx Graphics context
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Line color (RGBA8888)
 */
void evo_graphics_draw_line(evo_graphics_context_t *ctx, int x1, int y1, 
                          int x2, int y2, uint32_t color);

/**
 * Draw a circle outline
 * 
 * @param ctx Graphics context
 * @param center_x Center X coordinate
 * @param center_y Center Y coordinate
 * @param radius Circle radius
 * @param color Circle color (RGBA8888)
 */
void evo_graphics_draw_circle(evo_graphics_context_t *ctx, int center_x, int center_y, 
                            int radius, uint32_t color);

/**
 * Draw a filled circle
 * 
 * @param ctx Graphics context
 * @param center_x Center X coordinate
 * @param center_y Center Y coordinate
 * @param radius Circle radius
 * @param color Fill color (RGBA8888)
 */
void evo_graphics_draw_filled_circle(evo_graphics_context_t *ctx, int center_x, int center_y, 
                                   int radius, uint32_t color);

/* ============================================================================
 * NOTE: Text rendering functions have been moved to evo_text.h library
 * Include evo_text.h for text drawing capabilities
 * ============================================================================ */

/* ============================================================================
 * TRANSPARENCY AND ALPHA FUNCTIONS
 * ============================================================================ */

/**
 * Clear surface with transparency (alpha = 0, fully transparent)
 * 
 * @param ctx Graphics context
 */
void evo_graphics_clear_transparent(evo_graphics_context_t *ctx);

/**
 * Draw semi-transparent overlay rectangle
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param base_color Base color (RGB portion)
 * @param alpha Alpha value (0-255)
 */
void evo_graphics_draw_alpha_overlay(evo_graphics_context_t *ctx, int x, int y, 
                                   int width, int height, uint32_t base_color, uint8_t alpha);

/**
 * Create gradient transparency effect
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param base_color Base color (RGB portion)
 * @param start_alpha Starting alpha value
 * @param end_alpha Ending alpha value
 * @param horizontal True for horizontal gradient, false for vertical
 */
void evo_graphics_draw_alpha_gradient(evo_graphics_context_t *ctx, int x, int y, 
                                     int width, int height, uint32_t base_color,
                                     uint8_t start_alpha, uint8_t end_alpha, int horizontal);

/**
 * Draw pulsing alpha effect (for animations)
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param base_color Base color (RGB portion)
 * @param pulse_frame Current animation frame for pulsing
 * @param pulse_speed Speed of pulsing (lower = faster)
 */
void evo_graphics_draw_pulsing_alpha(evo_graphics_context_t *ctx, int x, int y, 
                                    int width, int height, uint32_t base_color,
                                    int pulse_frame, int pulse_speed);

/**
 * Create window-like semi-transparent panel with border
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Panel width
 * @param height Panel height
 * @param bg_color Background color (RGB portion)
 * @param border_color Border color
 * @param bg_alpha Background alpha
 * @param border_alpha Border alpha
 */
void evo_graphics_draw_alpha_panel(evo_graphics_context_t *ctx, int x, int y, 
                                  int width, int height, uint32_t bg_color, uint32_t border_color,
                                  uint8_t bg_alpha, uint8_t border_alpha);

/* ============================================================================
 * ALPHA BLENDING FUNCTIONS
 * ============================================================================ */

/**
 * Blend two colors using alpha compositing
 */
uint32_t evo_graphics_blend_colors(uint32_t src_color, uint32_t dst_color);

/**
 * Draw a rectangle with proper alpha blending over existing background
 */
void evo_graphics_draw_rectangle_alpha_blended(evo_graphics_context_t *ctx, int x, int y, 
                                             int width, int height, uint32_t color);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Draw a test pattern for display verification
 * 
 * @param ctx Graphics context
 */
void evo_graphics_draw_test_pattern(evo_graphics_context_t *ctx);

/**
 * Blend two colors with alpha
 * 
 * @param src Source color (RGBA8888)
 * @param dst Destination color (RGBA8888)
 * @return Blended color (RGBA8888)
 */
uint32_t evo_graphics_blend_colors(uint32_t src, uint32_t dst);

/**
 * Create color from RGB components
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param a Alpha component (0-255)
 * @return RGBA8888 color
 */
uint32_t evo_graphics_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Enhanced background patterns for transparency visualization */
void evo_graphics_draw_checkerboard(evo_graphics_context_t *ctx, int x, int y, 
                                   int width, int height, int square_size, 
                                   uint32_t color1, uint32_t color2);

/**
 * Fill a rectangle (alias for evo_graphics_draw_rectangle for compatibility)
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Fill color (RGBA8888)
 */
void evo_graphics_fill_rectangle(evo_graphics_context_t *ctx, int x, int y, 
                               int width, int height, uint32_t color);

/**
 * Alpha blending version of filled circle drawing function
 * 
 * @param ctx Graphics context
 * @param center_x Center X coordinate
 * @param center_y Center Y coordinate
 * @param radius Circle radius
 * @param color Fill color (RGBA8888)
 * @param alpha Alpha value (0-255)
 */
void evo_graphics_draw_filled_circle_alpha(evo_graphics_context_t *ctx, int center_x, int center_y, 
                                          int radius, uint32_t color, uint8_t alpha);

#ifdef __cplusplus
}
#endif

#endif /* EVO_GRAPHICS_H */

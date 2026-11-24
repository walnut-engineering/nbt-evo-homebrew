/**
 * @file evo_graphics.h
 * @brief EVO Core Graphics Drawing API
 * 
 * Provides basic 2D drawing primitives with a clean, simple interface.
 * This module focuses on immediate-mode drawing operations.
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_GRAPHICS_H
#define EVO_GRAPHICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
#ifndef EVO_CONTEXT_T_DEFINED
#define EVO_CONTEXT_T_DEFINED
typedef struct evo_context evo_context_t;
#endif

/* ============================================================================
 * COLOR UTILITIES
 * ============================================================================ */

/**
 * Create 32-bit RGBA color
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param a Alpha component (0-255, 255 = opaque)
 * @return 32-bit RGBA color value
 */
#define EVO_COLOR_RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

/**
 * Create 32-bit RGB color (opaque)
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 32-bit RGBA color value with full opacity
 */
#define EVO_COLOR_RGB(r, g, b) EVO_COLOR_RGBA(r, g, b, 255)

/* Common colors */
#define EVO_COLOR_BLACK     EVO_COLOR_RGB(0, 0, 0)
#define EVO_COLOR_WHITE     EVO_COLOR_RGB(255, 255, 255)
#define EVO_COLOR_RED       EVO_COLOR_RGB(255, 0, 0)
#define EVO_COLOR_GREEN     EVO_COLOR_RGB(0, 255, 0)
#define EVO_COLOR_BLUE      EVO_COLOR_RGB(0, 0, 255)
#define EVO_COLOR_YELLOW    EVO_COLOR_RGB(255, 255, 0)
#define EVO_COLOR_CYAN      EVO_COLOR_RGB(0, 255, 255)
#define EVO_COLOR_MAGENTA   EVO_COLOR_RGB(255, 0, 255)
#define EVO_COLOR_GRAY      EVO_COLOR_RGB(128, 128, 128)
#define EVO_COLOR_ORANGE    EVO_COLOR_RGB(255, 165, 0)

/* ============================================================================
 * BASIC DRAWING OPERATIONS
 * ============================================================================ */

/**
 * Clear context to specified color
 * 
 * @param ctx Graphics context
 * @param color 32-bit RGBA color
 */
void evo_clear(evo_context_t* ctx, uint32_t color);

/**
 * Set a single pixel
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 32-bit RGBA color
 */
void evo_set_pixel(evo_context_t* ctx, int x, int y, uint32_t color);

/**
 * Get a single pixel color
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @return 32-bit RGBA color
 */
uint32_t evo_get_pixel(evo_context_t* ctx, int x, int y);

/* ============================================================================
 * LINE DRAWING
 * ============================================================================ */

/**
 * Draw a line between two points
 * 
 * @param ctx Graphics context
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color 32-bit RGBA color
 */
void evo_draw_line(evo_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color);

/**
 * Draw a horizontal line
 * 
 * @param ctx Graphics context
 * @param x Start X coordinate
 * @param y Y coordinate
 * @param width Line width in pixels
 * @param color 32-bit RGBA color
 */
void evo_draw_hline(evo_context_t* ctx, int x, int y, int width, uint32_t color);

/**
 * Draw a vertical line
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Start Y coordinate
 * @param height Line height in pixels
 * @param color 32-bit RGBA color
 */
void evo_draw_vline(evo_context_t* ctx, int x, int y, int height, uint32_t color);

/* ============================================================================
 * RECTANGLE DRAWING
 * ============================================================================ */

/**
 * Draw a filled rectangle
 * 
 * @param ctx Graphics context
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color 32-bit RGBA color
 */
void evo_draw_rect(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color);

/**
 * Draw a rectangle outline
 * 
 * @param ctx Graphics context
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color 32-bit RGBA color
 */
void evo_draw_rect_outline(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color);

/**
 * Draw a rectangle with specified line thickness
 * 
 * @param ctx Graphics context
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param thickness Outline thickness in pixels
 * @param color 32-bit RGBA color
 */
void evo_draw_rect_thick(evo_context_t* ctx, int x, int y, int width, int height, int thickness, uint32_t color);

/* ============================================================================
 * CIRCLE DRAWING
 * ============================================================================ */

/**
 * Draw a filled circle
 * 
 * @param ctx Graphics context
 * @param x Circle center X
 * @param y Circle center Y
 * @param radius Circle radius
 * @param color 32-bit RGBA color
 */
void evo_draw_circle(evo_context_t* ctx, int x, int y, int radius, uint32_t color);

/**
 * Draw a circle outline
 * 
 * @param ctx Graphics context
 * @param x Circle center X
 * @param y Circle center Y
 * @param radius Circle radius
 * @param color 32-bit RGBA color
 */
void evo_draw_circle_outline(evo_context_t* ctx, int x, int y, int radius, uint32_t color);

/* ============================================================================
 * ADVANCED SHAPES
 * ============================================================================ */

/**
 * Draw a filled triangle
 * 
 * @param ctx Graphics context
 * @param x1 First vertex X
 * @param y1 First vertex Y
 * @param x2 Second vertex X
 * @param y2 Second vertex Y
 * @param x3 Third vertex X
 * @param y3 Third vertex Y
 * @param color 32-bit RGBA color
 */
void evo_draw_triangle(evo_context_t* ctx, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);

/**
 * Draw an ellipse
 * 
 * @param ctx Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param rx Horizontal radius
 * @param ry Vertical radius
 * @param color 32-bit RGBA color
 */
void evo_draw_ellipse(evo_context_t* ctx, int x, int y, int rx, int ry, uint32_t color);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Initialize graphics subsystem
 * 
 * @return 0 on success, negative on error
 */
int evo_graphics_init(void);

/**
 * Shutdown graphics subsystem
 */
void evo_graphics_shutdown(void);

/**
 * Check if coordinates are within context bounds
 * 
 * @param ctx Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @return 1 if within bounds, 0 if outside
 */
int evo_graphics_is_valid_coord(evo_context_t* ctx, int x, int y);

/**
 * Clip rectangle to context bounds
 * 
 * @param ctx Graphics context
 * @param x Rectangle X (will be modified)
 * @param y Rectangle Y (will be modified)
 * @param width Rectangle width (will be modified)
 * @param height Rectangle height (will be modified)
 * @return 1 if rectangle is visible after clipping, 0 if completely outside
 */
int evo_graphics_clip_rect(evo_context_t* ctx, int* x, int* y, int* width, int* height);

#ifdef __cplusplus
}
#endif

#endif /* EVO_GRAPHICS_H */

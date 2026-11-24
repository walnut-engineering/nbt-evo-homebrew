/**
 * @file evo_cairo.h
 * @brief EVO Graphics Cairo Integration Layer
 * 
 * This module provides Cairo 2D graphics integration for professional-quality
 * rendering with antialiasing, vector paths, and hardware acceleration.
 * 
 * Features:
 * - Cairo surface integration with EVO framebuffers
 * - Professional antialiased drawing primitives
 * - Vector path system for complex shapes (gear teeth)
 * - Backward compatibility with legacy primitives
 * - Runtime Cairo enable/disable for performance control
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_CAIRO_H
#define EVO_CAIRO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
#ifndef EVO_CONTEXT_T_DEFINED
#define EVO_CONTEXT_T_DEFINED
typedef struct evo_context evo_context_t;
#endif

/* ============================================================================
 * CAIRO AVAILABILITY CHECK
 * ============================================================================ */

/**
 * Check if Cairo library is available at runtime
 * 
 * @return 1 if Cairo is available, 0 if not
 */
int evo_cairo_is_available(void);

/**
 * Get Cairo version string
 * 
 * @return Cairo version or "unavailable" if not found
 */
const char* evo_cairo_get_version(void);

/* ============================================================================
 * CAIRO CONTEXT INTEGRATION
 * ============================================================================ */

/**
 * Cairo integration state for context
 */
typedef enum {
    EVO_CAIRO_DISABLED = 0,           /* Cairo integration disabled */
    EVO_CAIRO_ENABLED,                /* Cairo integration enabled */
    EVO_CAIRO_FALLBACK                /* Cairo failed, using fallback */
} evo_cairo_state_t;

/**
 * Cairo antialiasing modes
 */
typedef enum {
    EVO_CAIRO_ANTIALIAS_NONE = 0,     /* No antialiasing */
    EVO_CAIRO_ANTIALIAS_GRAY,         /* Gray-scale antialiasing */
    EVO_CAIRO_ANTIALIAS_SUBPIXEL,     /* Subpixel antialiasing (best quality) */
    EVO_CAIRO_ANTIALIAS_FAST,         /* Fast antialiasing */
    EVO_CAIRO_ANTIALIAS_GOOD,         /* Good quality antialiasing */
    EVO_CAIRO_ANTIALIAS_BEST          /* Best quality antialiasing (slowest) */
} evo_cairo_antialias_t;

/**
 * Initialize Cairo integration for a context
 * 
 * @param ctx Graphics context
 * @param enable_cairo 1 to enable Cairo, 0 to disable
 * @param antialias Antialiasing mode
 * @return 0 on success, negative on error
 */
int evo_cairo_init_context(evo_context_t* ctx, int enable_cairo, evo_cairo_antialias_t antialias);

/**
 * Shutdown Cairo integration for a context
 * 
 * @param ctx Graphics context
 */
void evo_cairo_shutdown_context(evo_context_t* ctx);

/**
 * Enable/disable Cairo for a context at runtime
 * 
 * @param ctx Graphics context
 * @param enabled 1 to enable, 0 to disable
 * @return 0 on success, negative on error
 */
int evo_cairo_set_enabled(evo_context_t* ctx, int enabled);

/**
 * Set Cairo antialiasing mode
 * 
 * @param ctx Graphics context
 * @param antialias Antialiasing mode
 * @return 0 on success, negative on error
 */
int evo_cairo_set_antialiasing(evo_context_t* ctx, evo_cairo_antialias_t antialias);

/**
 * Get Cairo integration state for context
 * 
 * @param ctx Graphics context
 * @return Cairo state
 */
evo_cairo_state_t evo_cairo_get_state(evo_context_t* ctx);

/* ============================================================================
 * CAIRO ENHANCED DRAWING PRIMITIVES
 * ============================================================================ */

/**
 * Draw antialiased line using Cairo
 * 
 * @param ctx Graphics context
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color 32-bit RGBA color
 * @param line_width Line width (1.0 for default)
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_line(evo_context_t* ctx, double x1, double y1, double x2, double y2, 
                        uint32_t color, double line_width);

/**
 * Draw antialiased circle using Cairo
 * 
 * @param ctx Graphics context
 * @param center_x Circle center X
 * @param center_y Circle center Y
 * @param radius Circle radius
 * @param color 32-bit RGBA color
 * @param line_width Line width (0 for filled circle)
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_circle(evo_context_t* ctx, double center_x, double center_y, double radius, 
                          uint32_t color, double line_width);

/**
 * Draw filled circle using Cairo
 * 
 * @param ctx Graphics context
 * @param center_x Circle center X
 * @param center_y Circle center Y
 * @param radius Circle radius
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_filled_circle(evo_context_t* ctx, double center_x, double center_y, 
                                 double radius, uint32_t color);

/**
 * Draw antialiased rectangle using Cairo
 * 
 * @param ctx Graphics context
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color 32-bit RGBA color
 * @param line_width Line width (0 for filled rectangle)
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_rectangle(evo_context_t* ctx, double x, double y, double width, double height, 
                             uint32_t color, double line_width);

/* ============================================================================
 * CAIRO VECTOR PATH SYSTEM
 * ============================================================================ */

/**
 * Vector path handle for complex shapes
 */
typedef struct evo_cairo_path evo_cairo_path_t;

/**
 * Gradient types for advanced effects
 */
typedef enum {
    EVO_CAIRO_GRADIENT_LINEAR = 0,    /* Linear gradient */
    EVO_CAIRO_GRADIENT_RADIAL         /* Radial gradient */
} evo_cairo_gradient_type_t;

/**
 * Create a new vector path
 * 
 * @return Path handle or NULL on error
 */
evo_cairo_path_t* evo_cairo_path_create(void);

/**
 * Destroy a vector path
 * 
 * @param path Path handle to destroy
 */
void evo_cairo_path_destroy(evo_cairo_path_t* path);

/**
 * Move path cursor to position
 * 
 * @param path Path handle
 * @param x X coordinate
 * @param y Y coordinate
 * @return 0 on success, negative on error
 */
int evo_cairo_path_move_to(evo_cairo_path_t* path, double x, double y);

/**
 * Add line to path
 * 
 * @param path Path handle
 * @param x X coordinate
 * @param y Y coordinate
 * @return 0 on success, negative on error
 */
int evo_cairo_path_line_to(evo_cairo_path_t* path, double x, double y);

/**
 * Add arc to path (perfect for gear teeth)
 * 
 * @param path Path handle
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param radius Arc radius
 * @param start_angle Start angle in radians
 * @param end_angle End angle in radians
 * @return 0 on success, negative on error
 */
int evo_cairo_path_arc(evo_cairo_path_t* path, double cx, double cy, double radius, 
                       double start_angle, double end_angle);

/**
 * Close the current path
 * 
 * @param path Path handle
 * @return 0 on success, negative on error
 */
int evo_cairo_path_close(evo_cairo_path_t* path);

/**
 * Draw (stroke) a vector path
 * 
 * @param ctx Graphics context
 * @param path Path to draw
 * @param color 32-bit RGBA color
 * @param line_width Line width
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_path(evo_context_t* ctx, evo_cairo_path_t* path, uint32_t color, double line_width);

/**
 * Fill a vector path
 * 
 * @param ctx Graphics context
 * @param path Path to fill
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_cairo_fill_path(evo_context_t* ctx, evo_cairo_path_t* path, uint32_t color);

/* ============================================================================
 * CAIRO PROFESSIONAL EFFECTS
 * ============================================================================ */

/**
 * Draw gradient-filled rectangle
 * 
 * @param ctx Graphics context
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param width Rectangle width
 * @param height Rectangle height
 * @param start_color Start color (32-bit RGBA)
 * @param end_color End color (32-bit RGBA)
 * @param gradient_type Gradient type
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_gradient_rectangle(evo_context_t* ctx, double x, double y, double width, double height,
                                      uint32_t start_color, uint32_t end_color, 
                                      evo_cairo_gradient_type_t gradient_type);

/**
 * Professional gear tooth drawing using Cairo paths
 * This replaces crude line-based gear rendering with perfect vector shapes
 * 
 * @param ctx Graphics context
 * @param center_x Gear center X
 * @param center_y Gear center Y
 * @param inner_radius Inner radius
 * @param outer_radius Outer radius
 * @param start_angle Tooth start angle in radians
 * @param end_angle Tooth end angle in radians
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_cairo_draw_gear_tooth(evo_context_t* ctx, double center_x, double center_y,
                              double inner_radius, double outer_radius,
                              double start_angle, double end_angle, uint32_t color);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Convert EVO RGBA color to Cairo color components
 * 
 * @param evo_color EVO 32-bit RGBA color
 * @param r Output: Red component (0.0-1.0)
 * @param g Output: Green component (0.0-1.0)
 * @param b Output: Blue component (0.0-1.0)
 * @param a Output: Alpha component (0.0-1.0)
 */
void evo_cairo_color_to_rgba(uint32_t evo_color, double* r, double* g, double* b, double* a);

/**
 * Flush Cairo drawing operations to framebuffer
 * Call this after Cairo drawing operations to ensure visibility
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_cairo_flush(evo_context_t* ctx);

/**
 * Get Cairo performance statistics
 * 
 * @param ctx Graphics context
 * @param cairo_calls Output: Number of Cairo calls made
 * @param fallback_calls Output: Number of fallback calls made
 * @return 0 on success, negative on error
 */
int evo_cairo_get_stats(evo_context_t* ctx, int* cairo_calls, int* fallback_calls);

/* ============================================================================
 * PHASE 8.6 UNIFIED CAIRO+TEXT FUNCTIONS
 * ============================================================================ */

/**
 * Draw text using unified Cairo+Text system with font support
 * 
 * @param ctx Graphics context
 * @param text Text to draw
 * @param x X coordinate
 * @param y Y coordinate  
 * @param font_family Font family name (e.g., "Arial")
 * @param font_size Font size in points
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_cairo_unified_draw_text(evo_context_t* ctx, const char* text, int x, int y, const char* font_family, float font_size, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* EVO_CAIRO_H */

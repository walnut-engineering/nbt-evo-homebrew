/**
 * @file evo_graphics_lib.h
 * @brief EVO Graphics Library - Master Header (Single Include)
 * 
 * This is the ONLY header you need to include for the EVO Graphics Library.
 * It provides a unified, clean API for all graphics functionality.
 * 
 * Usage:
 *   #include "evo_graphics_lib.h"
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_GRAPHICS_LIB_H
#define EVO_GRAPHICS_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Library version */
#define EVO_GRAPHICS_VERSION_MAJOR 2
#define EVO_GRAPHICS_VERSION_MINOR 0  
#define EVO_GRAPHICS_VERSION_PATCH 1
#define EVO_GRAPHICS_VERSION "2.0.1"

/* C89 compatibility - bool type */
#ifndef __cplusplus
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
typedef int evo_bool_t;
#define EVO_TRUE 1
#define EVO_FALSE 0
#else
#include <stdbool.h>
typedef bool evo_bool_t;
#define EVO_TRUE true
#define EVO_FALSE false
#endif
#else
typedef bool evo_bool_t;
#define EVO_TRUE true
#define EVO_FALSE false
#endif

/* Core functionality - flat structure */
#include "evo_display.h"
#include "evo_graphics.h" 
#include "evo_context.h"
#include "evo_cairo.h"
#include "evo_logging.h"

/* Backend management - flat structure */
#include "evo_backend.h"

/* Text interface - flat structure */
#include "evo_text.h"

/* ============================================================================
 * LIBRARY INITIALIZATION
 * ============================================================================ */

/**
 * Initialize the EVO Graphics Library
 * Call this once at application startup
 * 
 * @return 0 on success, negative on error
 */
int evo_graphics_init(void);

/**
 * Shutdown the EVO Graphics Library
 * Call this once at application cleanup
 */
void evo_graphics_shutdown(void);

/**
 * Get library version string
 * 
 * @return Version string (e.g. "2.0.0")
 */
const char* evo_graphics_get_version(void);

/* ============================================================================
 * UNIFIED CONTEXT API
 * ============================================================================ */

/**
 * Unified graphics context - replaces all old context types
 */
#ifndef EVO_CONTEXT_T_DEFINED
#define EVO_CONTEXT_T_DEFINED
typedef struct evo_context evo_context_t;
#endif

/**
 * Create graphics context with automatic backend selection
 * 
 * @param width Window width
 * @param height Window height
 * @param x Window X position (0 for auto)
 * @param y Window Y position (0 for auto)
 * @return Graphics context or NULL on error
 */
evo_context_t* evo_context_create(int width, int height, int x, int y);

/**
 * Create graphics context with specific backend
 * 
 * @param width Window width
 * @param height Window height  
 * @param x Window X position (0 for auto)
 * @param y Window Y position (0 for auto)
 * @param backend Backend type to use
 * @return Graphics context or NULL on error
 */
evo_context_t* evo_context_create_with_backend(int width, int height, int x, int y, evo_backend_type_t backend);

/**
 * Show the graphics context (make window visible)
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_context_show(evo_context_t* ctx);

/**
 * Hide the graphics context
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_context_hide(evo_context_t* ctx);

/**
 * Present the current frame (swap buffers)
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_context_present(evo_context_t* ctx);

/**
 * Destroy graphics context
 * 
 * @param ctx Graphics context to destroy
 */
void evo_context_destroy(evo_context_t* ctx);

/* ============================================================================
 * UNIFIED DRAWING API (Simplified)
 * ============================================================================ */

/**
 * Clear context to color
 * 
 * @param ctx Graphics context
 * @param color 32-bit RGBA color (0xRRGGBBAA)
 */
void evo_clear(evo_context_t* ctx, uint32_t color);

/**
 * Draw a line
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
 * Draw a filled circle
 * 
 * @param ctx Graphics context
 * @param x Circle center X
 * @param y Circle center Y
 * @param radius Circle radius
 * @param color 32-bit RGBA color
 */
void evo_draw_circle(evo_context_t* ctx, int x, int y, int radius, uint32_t color);

/* ============================================================================
 * PERFORMANCE MONITORING API
 * ============================================================================ */

/**
 * Performance metrics structure
 */
typedef struct {
    float fps;                    /* Frames per second */
    float avg_frame_time_ms;      /* Average frame time in milliseconds */
    unsigned long total_frames;   /* Total frames rendered */
    evo_bool_t hardware_accel;    /* Hardware acceleration enabled */
} evo_performance_t;

/**
 * Get current performance metrics
 * 
 * @param ctx Graphics context
 * @param perf Performance structure to fill
 * @return 0 on success, negative on error
 */
int evo_get_performance(evo_context_t* ctx, evo_performance_t* perf);

/* ============================================================================
 * GPU BACKEND OPTIMIZATION API
 * ============================================================================ */

/**
 * GPU workload types for backend selection
 */
typedef enum {
    EVO_WORKLOAD_FILL,      /* Fill operations - favor Vivante GC320 */
    EVO_WORKLOAD_BLIT,      /* Blit operations - favor Vivante GC320 */
    EVO_WORKLOAD_RENDER,    /* Complex rendering - favor PowerVR SGX540 */
    EVO_WORKLOAD_TEXT,      /* Text rendering - favor PowerVR SGX540 */
    EVO_WORKLOAD_AUTO       /* Automatic selection based on context */
} evo_workload_type_t;

/**
 * Select optimal GPU backend for workload
 * 
 * @param ctx Graphics context
 * @param workload_type Type of graphics workload
 * @return 0 on success, negative on error
 */
int evo_optimize_gpu_backend(evo_context_t* ctx, evo_workload_type_t workload_type);

/* ============================================================================
 * MEMORY MANAGEMENT OPTIMIZATION API
 * ============================================================================ */

/**
 * Memory pool types for different use cases
 */
typedef enum {
    EVO_POOL_FRAMEBUFFER,   /* Framebuffer memory pool */
    EVO_POOL_TEXTURE,       /* Texture/surface memory pool */
    EVO_POOL_TEMPORARY,     /* Temporary scratch memory pool */
    EVO_POOL_COUNT
} evo_memory_pool_type_t;

/**
 * Memory usage statistics
 */
typedef struct {
    size_t total_allocated;     /* Total memory allocated (bytes) */
    size_t peak_usage;          /* Peak memory usage (bytes) */
    size_t pool_hits;           /* Number of pool cache hits */
    size_t pool_misses;         /* Number of pool cache misses */
} evo_memory_stats_t;

/**
 * Get memory usage statistics
 * 
 * @param ctx Graphics context
 * @param stats Memory statistics structure to fill
 * @return 0 on success, negative on error
 */
int evo_get_memory_stats(evo_context_t* ctx, evo_memory_stats_t* stats);

/**
 * Optimize memory pools for expected usage patterns
 * 
 * @param ctx Graphics context
 * @param pool_type Type of memory pool to optimize
 * @param expected_size Expected allocation size in bytes
 * @return 0 on success, negative on error
 */
int evo_optimize_memory_pool(evo_context_t* ctx, evo_memory_pool_type_t pool_type, size_t expected_size);

/* ============================================================================
 * UNIFIED TEXT API (FreeType Only)
 * ============================================================================ */

/**
 * Draw text using FreeType
 * 
 * @param ctx Graphics context
 * @param text Text string to draw
 * @param x Text X position
 * @param y Text Y position
 * @param size Font size (use evo_font_size_t values)
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_draw_text(evo_context_t* ctx, const char* text, int x, int y, int size, uint32_t color);

/**
 * Get text width for layout calculations
 * 
 * @param text Text string
 * @param size Font size
 * @return Text width in pixels
 */
int evo_text_width(const char* text, int size);

/**
 * Get text height for layout calculations
 * 
 * @param size Font size
 * @return Text height in pixels
 */
int evo_text_height(int size);

/* ============================================================================
 * PHASE 8.4: RENDERING PIPELINE OPTIMIZATION API
 * ============================================================================ */

/**
 * Configure pipeline batching system
 * 
 * @param ctx Graphics context
 * @param enabled 1 to enable batching, 0 to disable
 * @param max_batch_size Maximum operations per batch (1-32)
 * @param auto_flush_threshold Auto-flush when this many ops queued
 * @return 0 on success, negative on error
 */
int evo_pipeline_configure_batching(evo_context_t* ctx, int enabled, int max_batch_size, int auto_flush_threshold);

/**
 * Manually flush the rendering pipeline
 * Forces immediate execution of all pending operations
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_pipeline_flush(evo_context_t* ctx);

/**
 * Configure buffer optimization level
 * 
 * @param ctx Graphics context  
 * @param level 0=none, 1=basic, 2=aggressive
 * @return 0 on success, negative on error
 */
int evo_pipeline_set_optimization_level(evo_context_t* ctx, int level);

/**
 * Get pipeline performance statistics
 * 
 * @param ctx Graphics context
 * @param batched_ops Number of operations batched in current session
 * @param flush_count Number of manual/auto flushes performed
 * @param state_syncs Number of state synchronizations performed
 * @return 0 on success, negative on error
 */
int evo_pipeline_get_stats(evo_context_t* ctx, int* batched_ops, int* flush_count, int* state_syncs);

/* ============================================================================
 * PHASE 8.6: STRATEGIC CAIRO GRAPHICS FIX - SOFTWARE FALLBACK API
 * ============================================================================ */

/**
 * Configure Cairo software fallback system
 * 
 * @param ctx Graphics context
 * @param software_fallback_enabled 1 to enable software fallback, 0 to disable
 * @param hardware_timeout_ms Timeout for hardware operations in milliseconds
 * @return 0 on success, negative on error
 */
int evo_cairo_configure_fallback(evo_context_t* ctx, int software_fallback_enabled, int hardware_timeout_ms);

/**
 * Get Cairo fallback system status and statistics
 * 
 * @param ctx Graphics context
 * @param blocking_detected 1 if hardware blocking was detected
 * @param fallback_active 1 if currently using software fallback
 * @param blocking_count Number of blocking operations detected
 * @param fallback_count Number of successful software fallbacks
 * @return 0 on success, negative on error
 */
int evo_cairo_get_fallback_stats(evo_context_t* ctx, int* blocking_detected, int* fallback_active, 
                                 int* blocking_count, int* fallback_count);

/**
 * Force Cairo graphics to use software fallback mode
 * Used for testing or when hardware acceleration is known to be problematic
 * 
 * @param ctx Graphics context
 * @param force_software 1 to force software mode, 0 to allow hardware
 * @return 0 on success, negative on error
 */
int evo_cairo_force_software_mode(evo_context_t* ctx, int force_software);

/**
 * Reset Cairo fallback system counters and state
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_cairo_reset_fallback_stats(evo_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* EVO_GRAPHICS_LIB_H */

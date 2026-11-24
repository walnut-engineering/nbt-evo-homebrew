/**
 * @file evo_core_unified.c
 * @brief EVO Unified Core Implementation - Context + Graphics
 * 
 * Phase 3 Consolidation: Merges context management and graphics rendering
 * into a single unified core module. This eliminates the artificial separation
 * between context lifecycle and drawing operations.
 * 
 * Features:
 * - Unified context management and rendering
 * - Cairo integration with legacy fallback
 * - Professional antialiased drawing primitives
 * - Complete context lifecycle management
 * - Display integration and buffer management
 * 
 * @version 2.1.0 - Phase 3 Core Consolidation
 * @date July 25, 2025
 */

#include "../include/evo_graphics_lib.h"
#include "../include/evo_logging.h"
#include "../include/evo_context.h"
#include "../include/evo_display.h"
#include "../include/evo_cairo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/evo_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ============================================================================
 * CONTEXT STRUCTURE DEFINITION
 * ============================================================================ */

/**
 * Unified graphics context structure
 * This replaces all previous context types with a single implementation
 */
struct evo_context {
    /* Display properties */
    int width;
    int height;
    int x;
    int y;
    
    /* Backend information */
    evo_backend_type_t requested_backend;
    evo_backend_type_t active_backend;
    const evo_backend_config_t* backend_config;
    evo_buffer_strategy_t buffer_strategy;
    
    /* Display integration */
    evo_display_t* display_ctx;
    
    /* Hardware acceleration */
    evo_screen_blit_context_t screen_blit;
    evo_gal_context_t gal;
    
    /* State */
    int visible;
    int initialized;
    
    /* ILM integration */
    uint32_t surface_id;
    uint32_t layer_id;
};

/* ============================================================================
 * CAIRO INTEGRATION COMPATIBILITY LAYER
 * ============================================================================ */

/**
 * Check if Cairo should be used for drawing operations
 * Returns 1 if Cairo is available and enabled, 0 for legacy fallback
 */
static int should_use_cairo(evo_context_t* ctx) {
    if (!ctx) return 0;
    
    // Check if Cairo is available and enabled for this context
    evo_cairo_state_t cairo_state = evo_cairo_get_state(ctx);
    return (cairo_state == EVO_CAIRO_ENABLED);
}

/* ============================================================================
 * LEGACY FALLBACK IMPLEMENTATIONS
 * ============================================================================ */

/**
 * Legacy Bresenham line drawing (fallback when Cairo unavailable)
 */
static void legacy_draw_line(evo_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color) {
    // TODO: Implement actual line drawing to framebuffer
}

/**
 * Legacy circle drawing (fallback when Cairo unavailable)
 */
static void legacy_draw_circle(evo_context_t* ctx, int x, int y, int radius, uint32_t color) {
    // TODO: Implement actual circle drawing to framebuffer
}

/**
 * Legacy filled circle drawing (fallback when Cairo unavailable)
 */
static void legacy_draw_filled_circle(evo_context_t* ctx, int x, int y, int radius, uint32_t color) {
    // TODO: Implement actual filled circle drawing to framebuffer
}

/**
 * Legacy rectangle drawing (fallback when Cairo unavailable)
 */
static void legacy_draw_rectangle(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color) {
    // TODO: Implement actual rectangle filling to framebuffer
}

/* ============================================================================
 * ENHANCED DRAWING FUNCTIONS WITH CAIRO INTEGRATION
 * ============================================================================ */

void evo_clear(evo_context_t* ctx, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for professional clearing with proper alpha handling
        printf("✨ CAIRO: clear (0x%08X)\n", color);
        evo_cairo_clear(ctx, color);
    } else {
        // Hardware-accelerated clear implementation
        printf("� HARDWARE: clear (0x%08X)\n", color);
        
        /* Get framebuffer from display context */
        uint32_t* framebuffer = NULL;
        if (ctx->display_ctx) {
            framebuffer = (uint32_t*)evo_display_get_framebuffer(ctx->display_ctx);
        }
        
        if (!framebuffer) {
            printf("⚠️  Warning: No framebuffer available for hardware clear\n");
            return;
        }
        
        /* Try GAL hardware acceleration first */
        if (evo_gal_is_ready(&ctx->gal)) {
            printf("🎯 Using GAL hardware acceleration\n");
            int result = evo_gal_clear_rect(&ctx->gal, framebuffer, 
                                            ctx->width, ctx->height,
                                            0, 0, ctx->width, ctx->height, color);
            if (result == 0) {
                printf("✅ GAL clear successful\n");
                return;
            } else {
                printf("⚠️  GAL clear failed: %s\n", evo_gal_get_error(&ctx->gal));
                printf("🔄 Falling back to Screen blit acceleration\n");
            }
        }
        
        /* Fallback to Screen blit acceleration */
        if (evo_screen_blit_is_ready(&ctx->screen_blit)) {
            printf("🎯 Using Screen blit acceleration\n");
            int result = evo_screen_blit_clear_rect(&ctx->screen_blit, framebuffer,
                                                    ctx->width, ctx->height,
                                                    0, 0, ctx->width, ctx->height, color);
            if (result == 0) {
                printf("✅ Screen blit clear successful\n");
                return;
            } else {
                printf("⚠️  Screen blit clear failed: %s\n", 
                       evo_screen_blit_get_error(&ctx->screen_blit));
            }
        }
        
        /* Software fallback */
        printf("💻 Falling back to software clear\n");
        for (int y = 0; y < ctx->height; y++) {
            for (int x = 0; x < ctx->width; x++) {
                framebuffer[y * ctx->width + x] = color;
            }
        }
        printf("✅ Software clear completed\n");
    }
}

void evo_set_pixel(evo_context_t* ctx, int x, int y, uint32_t color)
{
    if (!ctx) return;
    
    // Direct pixel access - no Cairo wrapper needed
    // TODO: Implement direct framebuffer pixel access
}

uint32_t evo_get_pixel(evo_context_t* ctx, int x, int y)
{
    if (!ctx) return 0;
    
    // Direct pixel access - no Cairo wrapper needed
    // TODO: Implement direct framebuffer pixel reading
    return 0;
}

void evo_draw_line(evo_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for antialiased line rendering
        printf("✨ CAIRO: line (%d,%d) to (%d,%d) color=0x%08X\n", x1, y1, x2, y2, color);
        evo_cairo_draw_line(ctx, (double)x1, (double)y1, (double)x2, (double)y2, color, 1.0);
    } else {
        // Fall back to legacy Bresenham algorithm
        printf("🔧 LEGACY: line (%d,%d) to (%d,%d) color=0x%08X\n", x1, y1, x2, y2, color);
        legacy_draw_line(ctx, x1, y1, x2, y2, color);
    }
}

void evo_draw_hline(evo_context_t* ctx, int x, int y, int width, uint32_t color)
{
    evo_draw_line(ctx, x, y, x + width - 1, y, color);
}

void evo_draw_vline(evo_context_t* ctx, int x, int y, int height, uint32_t color)
{
    evo_draw_line(ctx, x, y, x, y + height - 1, color);
}

void evo_draw_rect(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for professional filled rectangle with proper alpha handling
        printf("✨ CAIRO: filled rect (%d,%d) size %dx%d color=0x%08X\n", x, y, width, height, color);
        evo_cairo_draw_filled_rect(ctx, x, y, width, height, color);
    } else {
        // Fall back to legacy rectangle filling
        printf("🔧 LEGACY: filled rect (%d,%d) size %dx%d color=0x%08X\n", x, y, width, height, color);
        legacy_draw_rectangle(ctx, x, y, width, height, color);
    }
}

void evo_draw_rect_outline(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for professional outlined rectangle
        printf("✨ CAIRO: rect outline (%d,%d) size %dx%d color=0x%08X\n", x, y, width, height, color);
        evo_cairo_draw_rect(ctx, x, y, width, height, color);
    } else {
        // Draw outline using legacy line primitives
        printf("🔧 LEGACY: rect outline (%d,%d) size %dx%d color=0x%08X\n", x, y, width, height, color);
        evo_draw_hline(ctx, x, y, width, color);                    // Top
        evo_draw_hline(ctx, x, y + height - 1, width, color);       // Bottom
        evo_draw_vline(ctx, x, y, height, color);                   // Left
        evo_draw_vline(ctx, x + width - 1, y, height, color);       // Right
    }
}

void evo_draw_rect_thick(evo_context_t* ctx, int x, int y, int width, int height, int thickness, uint32_t color)
{
    if (!ctx || thickness <= 0) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for thick rectangle outline with proper stroke width
        printf("✨ CAIRO: thick rect (%d,%d) size %dx%d thickness=%d color=0x%08X\n", x, y, width, height, thickness, color);
        evo_cairo_draw_thick_rect(ctx, x, y, width, height, thickness, color);
    } else {
        // Draw thick outline using multiple thin rectangles
        printf("🔧 LEGACY: thick rect (%d,%d) size %dx%d thickness=%d color=0x%08X\n", x, y, width, height, thickness, color);
        for (int i = 0; i < thickness; i++) {
            evo_draw_rect_outline(ctx, x + i, y + i, width - 2*i, height - 2*i, color);
        }
    }
}

void evo_draw_circle(evo_context_t* ctx, int x, int y, int radius, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for antialiased filled circle
        printf("✨ CAIRO: filled circle (%d,%d) r=%d color=0x%08X\n", x, y, radius, color);
        evo_cairo_draw_filled_circle(ctx, x, y, radius, color);
    } else {
        // Fall back to legacy circle drawing
        printf("🔧 LEGACY: filled circle (%d,%d) r=%d color=0x%08X\n", x, y, radius, color);
        legacy_draw_filled_circle(ctx, x, y, radius, color);
    }
}

void evo_draw_circle_outline(evo_context_t* ctx, int x, int y, int radius, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for antialiased circle outline
        printf("✨ CAIRO: circle outline (%d,%d) r=%d color=0x%08X\n", x, y, radius, color);
        evo_cairo_draw_circle(ctx, (double)x, (double)y, (double)radius, color, 1.0);
    } else {
        // Fall back to legacy circle outline
        printf("🔧 LEGACY: circle outline (%d,%d) r=%d color=0x%08X\n", x, y, radius, color);
        legacy_draw_circle(ctx, x, y, radius, color);
    }
}

void evo_draw_triangle(evo_context_t* ctx, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for antialiased triangle
        printf("✨ CAIRO: triangle (%d,%d)-(%d,%d)-(%d,%d) color=0x%08X\n", x1, y1, x2, y2, x3, y3, color);
        evo_cairo_draw_triangle(ctx, x1, y1, x2, y2, x3, y3, color);
    } else {
        // Fall back to drawing three lines
        printf("🔧 LEGACY: triangle (%d,%d)-(%d,%d)-(%d,%d) color=0x%08X\n", x1, y1, x2, y2, x3, y3, color);
        evo_draw_line(ctx, x1, y1, x2, y2, color);
        evo_draw_line(ctx, x2, y2, x3, y3, color);
        evo_draw_line(ctx, x3, y3, x1, y1, color);
    }
}

void evo_draw_ellipse(evo_context_t* ctx, int x, int y, int rx, int ry, uint32_t color)
{
    if (!ctx) return;
    
    if (should_use_cairo(ctx)) {
        // Use Cairo for professional ellipse rendering
        printf("✨ CAIRO: ellipse (%d,%d) rx=%d ry=%d color=0x%08X\n", x, y, rx, ry, color);
        evo_cairo_draw_ellipse(ctx, x, y, rx, ry, color);
    } else {
        // Approximate ellipse with circle for legacy mode
        printf("🔧 LEGACY: ellipse->circle (%d,%d) r=%d color=0x%08X\n", x, y, (rx + ry) / 2, color);
        evo_draw_circle(ctx, x, y, (rx + ry) / 2, color);
    }
}

/* ============================================================================
 * GRAPHICS UTILITY FUNCTIONS
 * ============================================================================ */

int evo_graphics_is_valid_coord(evo_context_t* ctx, int x, int y)
{
    if (!ctx) return 0;
    return (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height);
}

int evo_graphics_clip_rect(evo_context_t* ctx, int* x, int* y, int* width, int* height)
{
    if (!ctx || !x || !y || !width || !height) return 0;
    
    // Clip rectangle to context bounds
    if (*x < 0) { *width += *x; *x = 0; }
    if (*y < 0) { *height += *y; *y = 0; }
    if (*x + *width > ctx->width) *width = ctx->width - *x;
    if (*y + *height > ctx->height) *height = ctx->height - *y;
    
    return (*width > 0 && *height > 0);
}

/* Legacy API compatibility functions */
void evo_graphics_draw_line(evo_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color) { 
    evo_draw_line(ctx, x1, y1, x2, y2, color); 
}

void evo_graphics_draw_circle(evo_context_t* ctx, int center_x, int center_y, int radius, uint32_t color) { 
    evo_draw_circle_outline(ctx, center_x, center_y, radius, color); 
}

void evo_graphics_draw_filled_circle(evo_context_t* ctx, int center_x, int center_y, int radius, uint32_t color) { 
    evo_draw_circle(ctx, center_x, center_y, radius, color); 
}

void evo_graphics_draw_rectangle(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color) { 
    evo_draw_rect(ctx, x, y, width, height, color); 
}

void evo_graphics_draw_rectangle_outline(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color) { 
    evo_draw_rect_outline(ctx, x, y, width, height, color); 
}

/* ============================================================================
 * CONTEXT MANAGEMENT FUNCTIONS
 * ============================================================================ */

/* Global context registry for debugging */
static evo_context_t* g_active_contexts[32];
static int g_context_count = 0;

/**
 * Register a context in the global registry for debugging
 */
static void register_context(evo_context_t* ctx) {
    if (g_context_count < 32) {
        g_active_contexts[g_context_count++] = ctx;
    }
}

/**
 * Unregister a context from the global registry
 */
static void unregister_context(evo_context_t* ctx) {
    for (int i = 0; i < g_context_count; i++) {
        if (g_active_contexts[i] == ctx) {
            // Shift remaining contexts down
            for (int j = i; j < g_context_count - 1; j++) {
                g_active_contexts[j] = g_active_contexts[j + 1];
            }
            g_context_count--;
            break;
        }
    }
}

/**
 * Get framebuffer access for direct pixel operations
 */
static uint32_t* get_framebuffer(evo_context_t* ctx) {
    if (!ctx || !ctx->display_ctx) return NULL;
    
    uint32_t* screen_buffer = evo_display_get_framebuffer(ctx->display_ctx);
    return screen_buffer;
}

/**
 * Allocate safe surface and layer IDs
 * Uses the working ID allocation strategy from the common library
 */
static void allocate_safe_ids(uint32_t* surface_id, uint32_t* layer_id) {
    // Use safe ID ranges that don't conflict with system layers.
    *surface_id = 2000;  // Known working surface ID
    *layer_id = 9000;    // User layer range that works with ILM

    /* A resident overlay (e.g. the homebrew launcher) that stays alive while it SPAWNS other
     * evo apps must NOT share these IDs: an app's teardown runs ilm_layerRemoveSurface(layer,
     * surface), which would yank the resident's surface off the shared layer and leave its
     * overlay live-but-invisible.  Let such a process pick its own non-conflicting IDs via env
     * (default stays 2000/9000, so transient full-screen apps are unaffected). */
    const char* es = getenv("EVO_SURFACE_ID");
    const char* el = getenv("EVO_LAYER_ID");
    if (es && *es) { unsigned long v = strtoul(es, NULL, 0); if (v) *surface_id = (uint32_t)v; }
    if (el && *el) { unsigned long v = strtoul(el, NULL, 0); if (v) *layer_id   = (uint32_t)v; }

    printf("🆔 Allocated safe IDs: surface=%u, layer=%u (user-created layer)\n", *surface_id, *layer_id);
    printf("🔍 Strategy: Creating new layer in 9000+ range to avoid system conflicts\n");
}

/* ============================================================================
 * PUBLIC CONTEXT PROPERTY ACCESS FUNCTIONS
 * ============================================================================ */

/**
 * Get framebuffer pointer for direct pixel access
 */
uint32_t* evo_context_get_framebuffer(evo_context_t* ctx) {
    if (!ctx) return NULL;
    return get_framebuffer(ctx);
}

/**
 * Check if context has hardware acceleration
 */
int evo_context_is_hardware_accelerated(evo_context_t* ctx) {
    if (!ctx) return 0;
    return (ctx->active_backend != EVO_BACKEND_SOFTWARE) ? 1 : 0;
}

/**
 * Get current backend type
 */
evo_backend_type_t evo_context_get_backend(evo_context_t* ctx) {
    if (!ctx) return EVO_BACKEND_SOFTWARE;
    return ctx->active_backend;
}

/**
 * Get context width
 */
int evo_context_get_width(evo_context_t* ctx) {
    return ctx ? ctx->width : 0;
}

/**
 * Get context height
 */
int evo_context_get_height(evo_context_t* ctx) {
    return ctx ? ctx->height : 0;
}

/**
 * Create a new graphics context
 */
evo_context_t* evo_context_create(int width, int height, int x, int y) {
    evo_context_t* ctx = calloc(1, sizeof(evo_context_t));
    if (!ctx) {
        fprintf(stderr, "[EVO] Failed to allocate context\n");
        return NULL;
    }
    
    // Initialize basic properties
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->visible = 0;
    ctx->initialized = 0;
    
    // Allocate safe IDs
    allocate_safe_ids(&ctx->surface_id, &ctx->layer_id);
    
    // Set default backend preferences
    ctx->requested_backend = EVO_BACKEND_AUTO;
    ctx->active_backend = EVO_BACKEND_POWERVR_2D;
    ctx->buffer_strategy = EVO_BUFFER_DOUBLE;
    
    // Create display context
    ctx->display_ctx = evo_display_create(width, height, x, y, ctx->surface_id, ctx->layer_id);
    if (!ctx->display_ctx) {
        fprintf(stderr, "[EVO] Failed to create display context\n");
        if (ctx->display_ctx) {
            evo_display_destroy(ctx->display_ctx);
        }
        free(ctx);
        return NULL;
    }
    
    ctx->initialized = 1;
    ctx->active_backend = EVO_BACKEND_POWERVR_2D;
    
    // Get backend configuration
    ctx->backend_config = evo_backend_get_config(ctx->active_backend);
    
    register_context(ctx);
    
    printf("[EVO] ✅ Context created successfully: %dx%d at (%d,%d)\n", width, height, x, y);
    return ctx;
}

/**
 * Create a new graphics context with specific backend preference
 */
evo_context_t* evo_context_create_with_backend(int width, int height, int x, int y, evo_backend_type_t backend) {
    evo_context_t* ctx = calloc(1, sizeof(evo_context_t));
    if (!ctx) {
        fprintf(stderr, "[EVO] Failed to allocate context\n");
        return NULL;
    }
    
    // Initialize basic properties
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->visible = 0;
    ctx->initialized = 0;
    
    // Allocate safe IDs
    allocate_safe_ids(&ctx->surface_id, &ctx->layer_id);
    
    // Set specific backend preference
    ctx->requested_backend = backend;
    ctx->active_backend = backend;  // Start with requested backend
    ctx->buffer_strategy = EVO_BUFFER_DOUBLE;
    
    printf("[EVO] Creating context with backend preference: %s\n", 
           backend == EVO_BACKEND_GAL ? "GAL Hardware" :
           backend == EVO_BACKEND_VIVANTE ? "Vivante GPU" :
           backend == EVO_BACKEND_POWERVR_2D ? "PowerVR 2D" : "Auto");
    
    // Create display context
    ctx->display_ctx = evo_display_create(width, height, x, y, ctx->surface_id, ctx->layer_id);
    if (!ctx->display_ctx) {
        fprintf(stderr, "[EVO] Failed to create display context\n");
        if (ctx->display_ctx) {
            evo_display_destroy(ctx->display_ctx);
        }
        free(ctx);
        return NULL;
    }
    
    ctx->initialized = 1;
    
    // Get backend configuration
    ctx->backend_config = evo_backend_get_config(ctx->active_backend);
    
    // Initialize the requested backend if specific
    if (backend == EVO_BACKEND_GAL) {
        printf("[EVO] Initializing GAL hardware acceleration backend\n");
        if (evo_gal_init(&ctx->gal) == 0) {
            printf("[EVO] ✅ GAL backend initialized successfully\n");
        } else {
            printf("[EVO] ⚠️ GAL backend initialization failed, will fallback\n");
        }
    }
    
    register_context(ctx);
    
    printf("[EVO] ✅ Context created successfully with backend: %s\n", 
           backend == EVO_BACKEND_GAL ? "GAL Hardware" :
           backend == EVO_BACKEND_VIVANTE ? "Vivante GPU" :
           backend == EVO_BACKEND_POWERVR_2D ? "PowerVR 2D" : "Auto");
    return ctx;
}

/**
 * Destroy a graphics context
 */
void evo_context_destroy(evo_context_t* ctx) {
    if (!ctx) return;
    
    printf("[EVO] Destroying context: %dx%d\n", ctx->width, ctx->height);
    
    unregister_context(ctx);
    
    /* Cleanup hardware acceleration contexts */
    if (evo_gal_is_ready(&ctx->gal)) {
        printf("[EVO] Shutting down GAL hardware acceleration\n");
        evo_gal_shutdown(&ctx->gal);
    }
    
    if (evo_screen_blit_is_ready(&ctx->screen_blit)) {
        printf("[EVO] Shutting down Screen blit acceleration\n");
        evo_screen_blit_shutdown(&ctx->screen_blit);
    }
    
    if (ctx->display_ctx) {
        evo_display_destroy(ctx->display_ctx);
    }
    
    free(ctx);
    printf("[EVO] ✅ Context destroyed\n");
}

/**
 * Show the context (make it visible)
 */
int evo_context_show(evo_context_t* ctx) {
    if (!ctx) {
        fprintf(stderr, "[EVO] ERROR: Invalid context for show\n");
        return -1;
    }
    
    if (ctx->display_ctx && evo_display_show(ctx->display_ctx) == 0) {
        ctx->visible = 1;
        printf("[EVO] ✅ Context shown\n");
        return 0;
    }
    
    fprintf(stderr, "[EVO] ERROR: Failed to show context\n");
    return -1;
}

/**
 * Hide the context (make it invisible)
 */
int evo_context_hide(evo_context_t* ctx) {
    if (!ctx) {
        fprintf(stderr, "[EVO] ERROR: Invalid context for hide\n");
        return -1;
    }
    
    if (ctx->display_ctx && evo_display_hide(ctx->display_ctx) == 0) {
        ctx->visible = 0;
        printf("[EVO] ✅ Context hidden\n");
        return 0;
    }
    
    fprintf(stderr, "[EVO] ERROR: Failed to hide context\n");
    return -1;
}

/**
 * Present/swap buffers for the context
 */
int evo_context_present(evo_context_t* ctx) {
    if (!ctx) {
        fprintf(stderr, "[EVO] ERROR: Invalid context for present\n");
        return -1;
    }
    
    if (ctx->display_ctx) {
        int result = evo_display_swap_buffers(ctx->display_ctx);
        if (result == 0) {
            EVO_LOG_BUFFERS_SWAPPED();
        }
        return result;
    }
    
    fprintf(stderr, "[EVO] ERROR: No display context for present\n");
    return -1;
}

/* ============================================================================
 * CONTEXT PROPERTY ACCESS FUNCTIONS
 * ============================================================================ */

int evo_context_get_dimensions(evo_context_t* ctx, int* width, int* height) {
    if (!ctx || !width || !height) return -1;
    *width = ctx->width;
    *height = ctx->height;
    return 0;
}

int evo_context_get_position(evo_context_t* ctx, int* x, int* y) {
    if (!ctx || !x || !y) return -1;
    *x = ctx->x;
    *y = ctx->y;
    return 0;
}

evo_context_info_t evo_context_get_info(evo_context_t* ctx) {
    evo_context_info_t info = {0};
    if (!ctx) return info;
    
    info.width = ctx->width;
    info.height = ctx->height;
    info.x = ctx->x;
    info.y = ctx->y;
    info.backend = ctx->active_backend;
    info.buffering = ctx->buffer_strategy;
    info.is_visible = ctx->visible;
    info.is_hardware_accelerated = (ctx->active_backend != EVO_BACKEND_SOFTWARE);
    info.backend_name = evo_backend_get_name(ctx->active_backend);
    
    return info;
}

int evo_context_is_visible(evo_context_t* ctx) {
    return ctx ? ctx->visible : 0;
}

int evo_context_is_initialized(evo_context_t* ctx) {
    return ctx ? ctx->initialized : 0;
}

/* ============================================================================
 * CONTEXT DISPLAY ACCESS FUNCTIONS
 * ============================================================================ */

evo_display_t* evo_context_get_display_context(evo_context_t* ctx) {
    return ctx ? ctx->display_ctx : NULL;
}

evo_display_t* evo_context_get_display(evo_context_t* ctx) {
    return evo_context_get_display_context(ctx);
}

/* ============================================================================
 * HARDWARE ACCELERATION FUNCTIONS
 * ============================================================================ */

int evo_context_get_screen_blit_context(evo_context_t* ctx, evo_screen_blit_context_t* blit_ctx) {
    if (!ctx || !blit_ctx) return -1;
    *blit_ctx = ctx->screen_blit;
    return 0;
}

evo_gal_context_t* evo_context_get_gal(evo_context_t* ctx) {
    if (!ctx) return NULL;
    return &ctx->gal;
}

int evo_context_enable_hardware_blit(evo_context_t* ctx) {
    if (!ctx) return -1;
    
    printf("[EVO] Enabling hardware acceleration for context\n");
    
    /* Initialize GAL hardware acceleration if GAL backend is selected */
    if (ctx->active_backend == EVO_BACKEND_GAL) {
        printf("[EVO] Initializing GAL hardware acceleration...\n");
        if (evo_gal_init(&ctx->gal) != 0) {
            printf("[EVO] ⚠️  GAL initialization failed: %s\n", evo_gal_get_error(&ctx->gal));
            printf("[EVO] Falling back to Screen blit acceleration\n");
            /* Continue with Screen blit as fallback */
        } else {
            printf("[EVO] ✅ GAL hardware acceleration enabled successfully\n");
            return 0; /* GAL is ready, skip Screen blit initialization */
        }
    }
    
    /* Initialize Screen blit acceleration (either as primary or fallback) */
    printf("[EVO] Initializing Screen blit acceleration...\n");
    if (evo_screen_blit_init(&ctx->screen_blit) != 0) {
        printf("[EVO] ⚠️  Screen blit initialization failed: %s\n", 
               evo_screen_blit_get_error(&ctx->screen_blit));
        return -1;
    }
    
    printf("[EVO] ✅ Hardware acceleration enabled successfully\n");
    return 0;
}

/* ============================================================================
 * DEBUGGING AND INFORMATION FUNCTIONS
 * ============================================================================ */

void evo_context_print_info(evo_context_t* ctx) {
    if (!ctx) {
        printf("[EVO] Context: NULL\n");
        return;
    }
    
    printf("[EVO] Context Info:\n");
    printf("  Dimensions: %dx%d at (%d,%d)\n", ctx->width, ctx->height, ctx->x, ctx->y);
    printf("  Backend: %d (requested: %d)\n", ctx->active_backend, ctx->requested_backend);
    printf("  Surface/Layer: %u/%u\n", ctx->surface_id, ctx->layer_id);
    printf("  State: %s, %s\n", 
           ctx->initialized ? "initialized" : "uninitialized",
           ctx->visible ? "visible" : "hidden");
    
    if (ctx->backend_config) {
        printf("  Backend Config: %s\n", evo_backend_get_name(ctx->active_backend));
    }
}

int evo_context_get_active_count(void) {
    return g_context_count;
}

void evo_context_print_all_active(void) {
    printf("[EVO] Active Contexts: %d\n", g_context_count);
    for (int i = 0; i < g_context_count; i++) {
        printf("  [%d] ", i);
        evo_context_print_info(g_active_contexts[i]);
    }
}

/* ============================================================================
 * CONTEXT UPDATES AND REFRESH
 * ============================================================================ */

/**
 * Update context state and refresh display
 */
int evo_context_update(evo_context_t* ctx) {
    if (!ctx) return -1;
    
    if (ctx->display_ctx) {
        evo_display_update(ctx->display_ctx);
    }
    
    return 0;
}

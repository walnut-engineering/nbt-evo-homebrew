/*
 * EVO Display Helper - Unified Screen + ILM Integration with Backend Selection
 * 
 * This helper library provides a simple, foolproof API for creating EVO applications
 * that combine Screen API with ILM (Input Layer Management) for actual visibility.
 * 
 * Based on proven industry patterns from working EVO infotainment systems.
 * Designed to eliminate common setup errors and provide automatic fallback.
 * 
 * Now integrated with the unified backend selection system for explicit control
 * over which graphics backend is used for rendering.
 * 
 * Key Features:
 * - Unified Screen + ILM integration in one simple API
 * - Automatic error recovery and fallback modes
 * - Built-in common drawing functions
 * - Foolproof state management
 * - EVO-only design for maximum performance and reliability
 * 
 * IMPORTANT: Surface ID Guidelines
 * ================================
 * To avoid conflicts with system-reserved surface IDs, use these ranges:
 * 
 * SAFE RANGES (recommended):
 * - 1500-1999: Application surfaces (low priority)
 * - 2000-2999: Application surfaces (normal priority) 
 * - 3000-3999: Application surfaces (high priority)
 * 
 * AVOID THESE RANGES (system reserved):
 * - 32-33, 80-81: System base ranges
 * - 256-521: System block allocation
 * - 528-537, 544-545, 592-593: System ranges
 * - 768-851: System block allocation
 * - 1024-1056: System ranges
 * - 65537+: High system ranges
 * - 131073+: Extended system ranges
 * 
 * The helper will warn you if you use potentially conflicting IDs.
 */

#ifndef EVO_DISPLAY_HELPER_H
#define EVO_DISPLAY_HELPER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <screen/screen.h>
#include "backends/evo_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * COMMON DEFINITIONS AND TYPES
 * ============================================================================ */

/* Common color definitions (RGBA8888) 
 * 
 * NOTE: These constants are legacy and will be deprecated.
 * Use EVO_COLOR_* constants from evo_graphics.h instead.
 * 
 * SYSTEM FORMAT COMPATIBILITY:
 * - Config specifies: format = rgba8888 (RGBA8888 logical format)  
 * - Memory layout: BGRA8888 (handled automatically by evo_display_clear)
 * - Hardware: PowerVR SGX540 + QNX Screen pipeline conversion
 */
#define COLOR_BLACK          0x000000FF
#define COLOR_WHITE          0xFFFFFFFF
#define COLOR_BLUE           0x0066CCFF
#define COLOR_GREEN          0x00CC66FF
#define COLOR_RED            0xCC0000FF
#define COLOR_ORANGE         0xFF9900FF
#define COLOR_GRAY           0x666666FF
#define COLOR_LIGHT_GRAY     0xCCCCCCFF
#define COLOR_DARK_BLUE      0x003366FF
#define COLOR_YELLOW         0xFFFF00FF
#define COLOR_PURPLE         0x9900CCFF
#define COLOR_MAGENTA        0xFF00FFFF
#define COLOR_CYAN           0x00CCFFFF

/* ILM type definitions */
typedef unsigned int t_ilm_uint;
typedef float t_ilm_float;
typedef unsigned char t_ilm_bool;
typedef t_ilm_uint t_ilm_surface;
typedef t_ilm_uint t_ilm_layer;
typedef void* t_ilm_nativehandle;

typedef enum {
    ILM_SUCCESS = 0,
    ILM_FAILED = -1,
    ILM_ERROR_INVALID_ARGUMENTS = -2,
    ILM_ERROR_RESOURCE_ALREADY_INUSE = -3,
    ILM_ERROR_RESOURCE_NOT_FOUND = -4,
    ILM_ERROR_NOT_IMPLEMENTED = -5,
    ILM_ERROR_ON_CONNECTION = -6
} t_ilm_errorTypes;

typedef enum {
    ILM_PIXELFORMAT_RGBA_8888 = 0,
    ILM_PIXELFORMAT_RGB_888 = 1,
    ILM_PIXELFORMAT_RGBA_4444 = 2,
    ILM_PIXELFORMAT_RGBA_5551 = 3,
    ILM_PIXELFORMAT_RGB_565 = 4,
    ILM_PIXELFORMAT_UNKNOWN = 255
} ilmPixelFormat;

/* ============================================================================
 * UNIFIED DISPLAY CONTEXT
 * ============================================================================ */

/* All-in-one display context that manages Screen + ILM integration */
typedef struct evo_display_context {
    /* Display properties */
    int width;
    int height;
    int x;
    int y;
    unsigned int surface_id;
    unsigned int layer_id;
    int visible;
    
    /* Screen API state */
    screen_context_t screen_ctx;
    screen_window_t screen_win;
    screen_buffer_t screen_buf[2];  /* Support for double buffering */
    char *screen_ptr[2];            /* Pointers to both buffers */
    int screen_stride;
    int buffer_count;               /* Number of buffers (1 or 2) */
    int current_buffer;             /* Current buffer index (0 or 1) */
    int front_buffer;               /* Front buffer index for display */
    
    /* Backend management */
    evo_backend_type_t selected_backend;         /* Currently selected backend */
    evo_backend_type_t requested_backend;        /* User-requested backend */
    const evo_backend_config_t *backend_config;  /* Backend configuration */
    evo_buffer_strategy_t buffer_strategy;       /* Buffer strategy from backend */
    int allow_backend_fallback;                  /* Allow fallback to other backends */
    
    /* ILM state */
    int ilm_available;
    int ilm_initialized;
    t_ilm_uint *original_render_order;
    t_ilm_uint original_render_count;
    
    /* Error tracking */
    int last_error;
    char error_message[256];
    
} evo_display_context_t;

/* ============================================================================
 * SIMPLIFIED API - MAIN FUNCTIONS
 * ============================================================================ */

/**
 * Create a new display context with automatic Screen + ILM integration.
 * This is the main entry point - it handles all complex setup automatically.
 * 
 * @param width Window width in pixels
 * @param height Window height in pixels  
 * @param x Window X position (0 for default)
 * @param y Window Y position (0 for default)
 * @param surface_id Unique surface ID for ILM (use 1500-3999 range for safety)
 * @param layer_id Unique layer ID for ILM (use 9999+ for high priority)
 * @return Display context or NULL on failure
 */
evo_display_context_t* evo_display_create(int width, int height, int x, int y, 
                                         unsigned int surface_id, unsigned int layer_id);

/**
 * Make the display visible on screen.
 * Handles all ILM integration automatically.
 * 
 * @param ctx Display context
 * @return 0 on success, -1 on failure
 */
int evo_display_show(evo_display_context_t *ctx);

/**
 * Update the display with current buffer content.
 * Call this after drawing operations to make changes visible.
 * 
 * @param ctx Display context
 * @return 0 on success, -1 on failure
 */
int evo_display_update(evo_display_context_t *ctx);

/**
 * Hide the display from screen.
 * 
 * @param ctx Display context
 * @return 0 on success, -1 on failure
 */
int evo_display_hide(evo_display_context_t *ctx);

/**
 * Destroy display context and clean up all resources.
 * Automatically handles ILM cleanup and Screen API cleanup.
 * 
 * @param ctx Display context (will be set to NULL)
 */
void evo_display_destroy(evo_display_context_t **ctx);

/**
 * Destroy display context and exit safely (avoids ILM cleanup crash).
 * This function does not return - it calls _exit() after cleanup.
 * Use this when the application is ending to avoid ILM destructor issues.
 * 
 * @param ctx Display context
 * @param exit_code Exit code for the application (usually 0)
 */
void evo_display_destroy_and_exit(evo_display_context_t **ctx, int exit_code);

/* ============================================================================
 * BACKEND-AWARE API FUNCTIONS
 * ============================================================================ */

/**
 * Create display context with specific backend configuration.
 * This provides full control over backend selection and capabilities.
 * 
 * @param width Window width in pixels
 * @param height Window height in pixels
 * @param x Window X position 
 * @param y Window Y position
 * @param surface_id Unique surface ID
 * @param layer_id Unique layer ID
 * @param backend_type Preferred backend type (EVO_BACKEND_AUTO for auto-select)
 * @param allow_fallback Allow fallback to other backends if preferred fails
 * @param require_double_buffer Require double buffer support
 * @param require_hw_accel Require hardware acceleration
 * @return Display context or NULL on failure
 */
/**
 * Create display context with explicit backend selection.
 * 
 * @param width Display width in pixels
 * @param height Display height in pixels
 * @param x X coordinate for positioning
 * @param y Y coordinate for positioning
 * @param surface_id Unique surface ID for ILM (use 2000+ for applications)
 * @param layer_id Unique layer ID for ILM (use 9999+ for high priority)
 * @param backend_type Preferred backend (use EVO_BACKEND_AUTO for auto-selection)
 * @param allow_fallback Allow fallback to other backends if preferred fails
 * @return Display context or NULL on failure
 */
evo_display_context_t* evo_display_create_with_backend(int width, int height, int x, int y,
                                                      unsigned int surface_id, unsigned int layer_id,
                                                      evo_backend_type_t backend_type,
                                                      int allow_fallback);

/**
 * Get backend information for the display context.
 * 
 * @param ctx Display context
 * @return Backend configuration or NULL if not available
 */
const evo_backend_config_t* evo_display_get_backend_info(evo_display_context_t *ctx);

/**
 * Print detailed backend status information.
 * 
 * @param ctx Display context
 */
void evo_display_print_backend_status(evo_display_context_t *ctx);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Clear the entire display with a solid color.
 * This is a basic buffer operation and stays in the helper.
 * 
 * @param ctx Display context
 * @param color Color in RGBA8888 format (use COLOR_* constants)
 */
void evo_display_clear(evo_display_context_t *ctx, unsigned int color);

/**
 * Get framebuffer pointer for use with external graphics libraries.
 * 
 * @param ctx Display context
 * @return Framebuffer pointer (RGBA8888) or NULL on error
 */
uint32_t* evo_display_get_framebuffer(evo_display_context_t *ctx);

/**
 * Get the next framebuffer for double buffering.
 * If double buffering is not enabled, returns the current framebuffer.
 * 
 * @param ctx Display context
 * @return Next framebuffer pointer or NULL on error
 */
uint32_t* evo_display_get_next_framebuffer(evo_display_context_t *ctx);

/**
 * Swap the front and back buffers for double buffering.
 * Call this after rendering to the back buffer to display the frame.
 * 
 * @param ctx Display context
 * @return 0 on success, -1 on failure
 */
int evo_display_swap_buffers(evo_display_context_t *ctx);

/**
 * Enable double buffering for smooth animation without flicker.
 * Must be called before evo_display_show().
 * 
 * @param ctx Display context  
 * @param enable 1 to enable double buffering, 0 for single buffer
 * @return 0 on success, -1 on failure
 */
int evo_display_enable_double_buffering(evo_display_context_t *ctx, int enable);

/**
 * Get display dimensions.
 * 
 * @param ctx Display context
 * @param width_out Pointer to store width
 * @param height_out Pointer to store height
 * @param stride_out Pointer to store stride in bytes
 */
void evo_display_get_dimensions(evo_display_context_t *ctx, 
                               int *width_out, int *height_out, int *stride_out);

/**
 * Get the last error message for debugging.
 * 
 * @param ctx Display context
 * @return Error message string
 */
const char* evo_display_get_error(evo_display_context_t *ctx);

/**
 * Check if ILM is available and working.
 * 
 * @param ctx Display context
 * @return 1 if ILM is available, 0 if not
 */
int evo_display_has_ilm(evo_display_context_t *ctx);

/**
 * Print display context information for debugging.
 * 
 * @param ctx Display context
 * @param prefix Prefix string for output
 */
void evo_display_print_info(evo_display_context_t *ctx, const char *prefix);

/**
 * Draw rectangle wrapper function (legacy compatibility).
 * This is a simple wrapper around the graphics library for convenience.
 * 
 * @param ctx Display context
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color Color in RGBA format
 */
void evo_display_draw_rectangle(evo_display_context_t *ctx, int x, int y, 
                               int width, int height, unsigned int color);

/**
 * Draw line wrapper function (legacy compatibility).
 * This is a simple wrapper around the graphics library for convenience.
 * 
 * @param ctx Display context
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Color in RGBA format
 */
void evo_display_draw_line(evo_display_context_t *ctx, int x1, int y1, 
                          int x2, int y2, unsigned int color);

/**
 * Draw circle wrapper function (legacy compatibility).
 * This is a simple wrapper around the graphics library for convenience.
 * 
 * @param ctx Display context
 * @param center_x Center X coordinate
 * @param center_y Center Y coordinate
 * @param radius Circle radius
 * @param color Color in RGBA format
 */
void evo_display_draw_circle(evo_display_context_t *ctx, int center_x, int center_y, 
                            int radius, unsigned int color);

/**
 * Draw filled circle wrapper function (legacy compatibility).
 * This is a simple wrapper around the graphics library for convenience.
 * 
 * @param ctx Display context
 * @param center_x Center X coordinate
 * @param center_y Center Y coordinate
 * @param radius Circle radius
 * @param color Color in RGBA format
 */
void evo_display_draw_filled_circle(evo_display_context_t *ctx, int center_x, int center_y, 
                                   int radius, unsigned int color);

/**
 * Create a simple test pattern for verification.
 * Useful for testing if the display is working correctly.
 * NOTE: This uses basic drawing and will be moved to graphics library eventually.
 * 
 * @param ctx Display context
 */
void evo_display_draw_test_pattern(evo_display_context_t *ctx);

/**
 * Validate surface ID for potential conflicts with system-reserved ranges.
 * Prints warnings if the ID might conflict with known system surfaces.
 * 
 * @param surface_id Surface ID to validate
 */
void evo_display_validate_surface_id(unsigned int surface_id);

/* ============================================================================
 * ENHANCED CONFIGURATION STRUCTURES
 * ============================================================================ */

/* Configuration structure for fine-tuned display creation */
typedef struct evo_display_config {
    /* Basic properties */
    int width;
    int height;
    int x;
    int y;
    unsigned int surface_id;
    unsigned int layer_id;
    
    /* Advanced positioning and sizing */
    int z_order;                /* Window stacking order */
    int source_x;               /* Source viewport X */
    int source_y;               /* Source viewport Y */
    int source_width;           /* Source viewport width */
    int source_height;          /* Source viewport height */
    
    /* Advanced color and format control */
    int pixel_format;           /* SCREEN_FORMAT_* (default: RGBA8888) */
    uint32_t background_color;  /* RGBA background color (default: transparent) */
    int usage_flags;            /* SCREEN_USAGE_* flags */
    
    /* Buffer configuration */
    int buffer_count;           /* Number of buffers (1=single, 2=double) */
    int enable_vsync;           /* Enable vertical sync */
    
    /* Advanced rendering options */
    int enable_color_correction; /* Enable color correction */
    int enable_alpha_blending;   /* Enable alpha blending */
    int force_exact_colors;      /* Force exact color matching */
    
    /* ILM composition settings */
    int layer_opacity;          /* 0-255 layer opacity (255=opaque) */
    int composition_mode;       /* ILM composition mode */
    uint32_t layer_background;  /* Layer background color */
} evo_display_config_t;

/**
 * Enable/disable advanced buffer management
 */
int evo_display_configure_buffers(evo_display_context_t *ctx, 
                                 int buffer_count, int enable_vsync);

/* ============================================================================
 * SAFE ID ALLOCATION
 * ============================================================================ */

/**
 * Allocate safe surface and layer IDs automatically
 */
typedef struct {
    unsigned int surface_id;
    unsigned int layer_id;
} evo_display_ids_t;

evo_display_ids_t evo_display_allocate_safe_ids(int priority_level);

#ifdef __cplusplus
}
#endif

#endif /* EVO_DISPLAY_HELPER_H */

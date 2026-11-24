/**
 * @file evo_display.h
 * @brief EVO Display Management (Renamed from evo_display_helper.h)
 * 
 * This module provides Screen and ILM integration for display management.
 * Maintains compatibility with existing display helper functionality.
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_DISPLAY_H
#define EVO_DISPLAY_H

#include <screen/screen.h>
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
 * DISPLAY SYSTEM TYPES
 * ============================================================================ */

/**
 * Display handle for ILM integration
 */
#ifndef EVO_DISPLAY_T_DEFINED
#define EVO_DISPLAY_T_DEFINED
typedef struct evo_display evo_display_t;
#endif

/**
 * Display configuration
 */
typedef struct {
    int width;                     /* Display width */
    int height;                    /* Display height */
    int x;                         /* Display X position */
    int y;                         /* Display Y position */
    uint32_t surface_id;          /* ILM surface ID */
    uint32_t layer_id;            /* ILM layer ID */
    int visible;                  /* Initial visibility */
} evo_display_config_t;

/* ============================================================================
 * DISPLAY LIFECYCLE
 * ============================================================================ */

/**
 * Create display context with ILM integration
 * 
 * @param width Display width
 * @param height Display height
 * @param x Display X position
 * @param y Display Y position
 * @param surface_id ILM surface ID
 * @param layer_id ILM layer ID
 * @return Display handle or NULL on error
 */
evo_display_t* evo_display_create(int width, int height, int x, int y, uint32_t surface_id, uint32_t layer_id);

/**
 * Create display with configuration
 * 
 * @param config Display configuration
 * @return Display handle or NULL on error
 */
evo_display_t* evo_display_create_ex(const evo_display_config_t* config);

/**
 * Destroy display context
 * 
 * @param display Display handle to destroy
 */
void evo_display_destroy(evo_display_t* display);

/* ============================================================================
 * DISPLAY CONTROL
 * ============================================================================ */

/**
 * Show display (make visible)
 * 
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_display_show(evo_display_t* display);

/**
 * Hide display
 * 
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_display_hide(evo_display_t* display);

/**
 * Move display to new position
 * 
 * @param display Display handle
 * @param x New X position
 * @param y New Y position
 * @return 0 on success, negative on error
 */
int evo_display_move(evo_display_t* display, int x, int y);

/**
 * Resize display
 * 
 * @param display Display handle
 * @param width New width
 * @param height New height
 * @return 0 on success, negative on error
 */
int evo_display_resize(evo_display_t* display, int width, int height);

/**
 * Set display opacity
 * 
 * @param display Display handle
 * @param opacity Opacity (0.0 = transparent, 1.0 = opaque)
 * @return 0 on success, negative on error
 */
int evo_display_set_opacity(evo_display_t* display, float opacity);

/* ============================================================================
 * LAYER MANAGEMENT UTILITIES (NEW IN V3.0)
 * ============================================================================ */

/**
 * Enable presentation mode - hide other layers to give application focus
 * 
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_display_enable_presentation_mode(evo_display_t* display);

/**
 * Disable presentation mode - restore visibility of previously hidden layers
 * 
 * @param display Display handle  
 * @return 0 on success, negative on error
 */
int evo_display_disable_presentation_mode(evo_display_t* display);

/**
 * Check if presentation mode is currently active
 * 
 * @return 1 if presentation mode is active, 0 if not
 */
int evo_display_is_presentation_mode(void);

/**
 * Raise our layer to the top of the render order so our overlay composites OVER the OEM HMI
 * WITHOUT hiding it (unlike presentation mode) - lets a translucent overlay float over the
 * still-visible, dimmed live HMI.  Idempotent; call on each summon in case the HMI re-ordered.
 * @return 0 on success, -1 on failure.
 */
int evo_display_raise_to_top(evo_display_t* display);

/**
 * Place this display's layer on the screen: the source rectangle selects the
 * part of the window to show, the destination rectangle where (and how large)
 * it appears. Used for picture-in-picture and docked layouts.
 */
int evo_display_set_layer_geometry(evo_display_t* display,
                                   int src_x, int src_y, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h);

/**
 * Resize another layer (for example the car HMI) to make room for a docked
 * layout. Restored to full screen automatically by evo_display_hide().
 */
int evo_display_set_foreign_layer_rect(evo_display_t* display, uint32_t layer_id, int x, int y, int w, int h);

/* ============================================================================
 * DISPLAY INFORMATION
 * ============================================================================ */

/**
 * Get display configuration
 * 
 * @param display Display handle
 * @return Display configuration structure
 */
evo_display_config_t evo_display_get_config(evo_display_t* display);

/**
 * Check if display is visible
 * 
 * @param display Display handle
 * @return 1 if visible, 0 if hidden
 */
int evo_display_is_visible(evo_display_t* display);

/**
 * Get display bounds
 * 
 * @param display Display handle
 * @param x Output: X position
 * @param y Output: Y position
 * @param width Output: width
 * @param height Output: height
 * @return 0 on success, negative on error
 */
int evo_display_get_bounds(evo_display_t* display, int* x, int* y, int* width, int* height);

/* ============================================================================
 * INTEGRATION WITH GRAPHICS CONTEXT
 * ============================================================================ */

/**
 * Attach display to graphics context for ILM integration
 * 
 * @param ctx Graphics context
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_context_attach_display(evo_context_t* ctx, evo_display_t* display);

/**
 * Detach display from graphics context
 * 
 * @param ctx Graphics context
 * @return 0 on success, negative on error
 */
int evo_context_detach_display(evo_context_t* ctx);

/* ============================================================================
 * BUFFER MANAGEMENT API
 * ============================================================================ */

/**
 * Get current framebuffer pointer for direct rendering
 * 
 * @param display Display handle
 * @return Pointer to current framebuffer or NULL on error
 */
uint32_t* evo_display_get_framebuffer(evo_display_t* display);

/**
 * Get next framebuffer for double buffering
 * 
 * @param display Display handle
 * @return Pointer to back buffer or NULL on error
 */
uint32_t* evo_display_get_next_framebuffer(evo_display_t* display);

/**
 * Update display by posting current buffer
 * 
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_display_update(evo_display_t* display);

/**
 * Swap front and back buffers for double buffering
 * 
 * @param display Display handle
 * @return 0 on success, negative on error
 */
int evo_display_swap_buffers(evo_display_t* display);

/* ============================================================================
 * FRAMEBUFFER ACCESS
 * ============================================================================ */

/**
 * Get framebuffer pointer for direct pixel access
 * 
 * @param display Display handle
 * @return Framebuffer pointer or NULL if not available
 */
uint32_t* evo_display_get_framebuffer(evo_display_t* display);

/* ============================================================================
 * LEGACY COMPATIBILITY
 * ============================================================================ */

/**
 * Legacy display helper structure (for backward compatibility)
 * Use evo_display_t for new code
 */
typedef evo_display_t evo_display_helper_t;

/* Legacy function aliases */
#define evo_display_helper_create evo_display_create
#define evo_display_helper_destroy evo_display_destroy
#define evo_display_helper_show evo_display_show
#define evo_display_helper_hide evo_display_hide

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================ */

/**
 * Initialize display system
 * 
 * @return 0 on success, negative on error
 */
int evo_display_init(void);

/**
 * Shutdown display system
 */
void evo_display_shutdown(void);

/**
 * Check if display system is initialized
 * 
 * @return 1 if initialized, 0 if not
 */
int evo_display_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_DISPLAY_H */

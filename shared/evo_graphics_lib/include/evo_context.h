/**
 * @file evo_context.h
 * @brief EVO Unified Graphics Context
 * 
 * This module provides a unified graphics context that replaces all the
 * previous context types with a single, clean API.
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_CONTEXT_H
#define EVO_CONTEXT_H

#include "evo_backend.h"
#include <screen/screen.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

#ifndef EVO_CONTEXT_T_DEFINED
#define EVO_CONTEXT_T_DEFINED
typedef struct evo_context evo_context_t;
#endif
#ifndef EVO_DISPLAY_T_DEFINED
#define EVO_DISPLAY_T_DEFINED
typedef struct evo_display evo_display_t;
#endif

/* ============================================================================
 * CONTEXT PROPERTIES
 * ============================================================================ */

/**
 * Context creation flags
 */
typedef enum {
    EVO_CONTEXT_DEFAULT = (1 << 2),    /* Default context options - visible */
    EVO_CONTEXT_HIDDEN = 0,            /* Hidden context */
    EVO_CONTEXT_VISIBLE = (1 << 2),    /* Make context visible immediately */
    EVO_CONTEXT_DOUBLE_BUFFER = (1 << 3), /* Enable double buffering */
    EVO_CONTEXT_VSYNC = (1 << 4)       /* Enable vertical sync */
} evo_context_flags_t;

/**
 * Context information structure
 */
typedef struct {
    int width;                         /* Context width in pixels */
    int height;                        /* Context height in pixels */
    int x;                             /* Window X position */
    int y;                             /* Window Y position */
    evo_backend_type_t backend;        /* Active backend type */
    evo_buffer_strategy_t buffering;   /* Buffer strategy */
    int is_visible;                    /* Visibility state */
    int is_hardware_accelerated;       /* Hardware acceleration status */
    const char* backend_name;          /* Backend name string */
} evo_context_info_t;

/* ============================================================================
 * CONTEXT LIFECYCLE
 * ============================================================================ */

/**
 * Create graphics context with automatic backend selection
 * 
 * @param width Context width
 * @param height Context height
 * @param x Window X position (0 for auto)
 * @param y Window Y position (0 for auto)
 * @return Graphics context or NULL on error
 */
evo_context_t* evo_context_create(int width, int height, int x, int y);

/**
 * Create graphics context with specific backend
 * 
 * @param width Context width
 * @param height Context height
 * @param x Window X position (0 for auto)
 * @param y Window Y position (0 for auto)
 * @param backend Backend type to use
 * @return Graphics context or NULL on error
 */
evo_context_t* evo_context_create_with_backend(int width, int height, int x, int y, evo_backend_type_t backend);

/**
 * Create graphics context with flags
 * 
 * @param width Context width
 * @param height Context height
 * @param x Window X position (0 for auto)
 * @param y Window Y position (0 for auto)
 * @param backend Backend type to use (EVO_BACKEND_AUTO for auto-select)
 * @param flags Context creation flags
 * @return Graphics context or NULL on error
 */
evo_context_t* evo_context_create_ex(int width, int height, int x, int y, evo_backend_type_t backend, evo_context_flags_t flags);

/**
 * Destroy graphics context
 * 
 * @param ctx Graphics context to destroy
 */
void evo_context_destroy(evo_context_t* ctx);

/* ============================================================================
 * CONTEXT CONTROL
 * ============================================================================ */

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
 * Resize the graphics context
 * 
 * @param ctx Graphics context
 * @param width New width
 * @param height New height
 * @return 0 on success, negative on error
 */
int evo_context_resize(evo_context_t* ctx, int width, int height);

/**
 * Move the graphics context
 * 
 * @param ctx Graphics context
 * @param x New X position
 * @param y New Y position
 * @return 0 on success, negative on error
 */
int evo_context_move(evo_context_t* ctx, int x, int y);

/* ============================================================================
 * CONTEXT INFORMATION
 * ============================================================================ */

/**
 * Get context information
 * 
 * @param ctx Graphics context
 * @return Context information structure
 */
evo_context_info_t evo_context_get_info(evo_context_t* ctx);

/**
 * Get context width
 * 
 * @param ctx Graphics context
 * @return Width in pixels
 */
int evo_context_get_width(evo_context_t* ctx);

/**
 * Get context height
 * 
 * @param ctx Graphics context
 * @return Height in pixels
 */
int evo_context_get_height(evo_context_t* ctx);

/**
 * Get current backend type
 * 
 * @param ctx Graphics context
 * @return Backend type
 */
evo_backend_type_t evo_context_get_backend(evo_context_t* ctx);

/**
 * Check if context is visible
 * 
 * @param ctx Graphics context
 * @return 1 if visible, 0 if hidden
 */
int evo_context_is_visible(evo_context_t* ctx);

/**
 * Check if context has hardware acceleration
 * 
 * @param ctx Graphics context
 * @return 1 if hardware accelerated, 0 if software
 */
int evo_context_is_hardware_accelerated(evo_context_t* ctx);

/* ============================================================================
 * DISPLAY ACCESS
 * ============================================================================ */

/**
 * Get display context for text rendering and framebuffer access
 * 
 * @param ctx Graphics context
 * @return Display context or NULL
 */
evo_display_t* evo_context_get_display(evo_context_t* ctx);

/**
 * Get display context (compatibility alias)
 * 
 * @param ctx Graphics context
 * @return Display context or NULL
 */
evo_display_t* evo_context_get_display_context(evo_context_t* ctx);

/**
 * Get framebuffer pointer for direct pixel access
 * 
 * @param ctx Graphics context
 * @return Framebuffer pointer or NULL
 */
uint32_t* evo_context_get_framebuffer(evo_context_t* ctx);

/**
 * Get next framebuffer for double buffering
 * 
 * @param ctx Graphics context
 * @return Next framebuffer pointer or NULL
 */
uint32_t* evo_context_get_next_framebuffer(evo_context_t* ctx);

/* ============================================================================
 * LOW-LEVEL ACCESS (Advanced Usage)
 * ============================================================================ */

/**
 * Get raw Screen context (for advanced users)
 * 
 * @param ctx Graphics context
 * @return Screen context handle or NULL
 */
screen_context_t evo_context_get_screen_context(evo_context_t* ctx);

/**
 * Get raw Screen window (for advanced users)
 * 
 * @param ctx Graphics context
 * @return Screen window handle or NULL
 */
screen_window_t evo_context_get_screen_window(evo_context_t* ctx);

/**
 * Get current drawing buffer (for advanced users)
 * 
 * @param ctx Graphics context
 * @return Screen buffer handle or NULL
 */
screen_buffer_t evo_context_get_current_buffer(evo_context_t* ctx);

/**
 * Get Screen blit context for hardware acceleration (for advanced users)
 * 
 * @param ctx Graphics context
 * @return Pointer to Screen blit context or NULL if not available
 */
evo_screen_blit_context_t* evo_context_get_screen_blit(evo_context_t* ctx);

/**
 * Get GAL context for direct hardware acceleration (for advanced users)
 * 
 * @param ctx Graphics context
 * @return Pointer to GAL context or NULL if not available
 */
evo_gal_context_t* evo_context_get_gal(evo_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* EVO_CONTEXT_H */

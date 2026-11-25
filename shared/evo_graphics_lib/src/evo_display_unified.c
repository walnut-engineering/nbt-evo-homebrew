/**
 * @file evo_display_unified.c
 * @brief EVO Unified Display Implementation
 * 
 * Migrated from evo_display_helper.c with essential working functions.
 */

#include "../include/evo_display.h"
#include "../include/evo_logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <screen/screen.h>
#include <ctype.h>
#include <unistd.h>

/* Enable popen/pclose on QNX */
#ifdef __QNX__
#define _GNU_SOURCE
#endif

/* Global display state */
static int g_display_initialized = 0;

/* ILM library handle */
static void *g_ilm_handle = NULL;

/* ============================================================================
 * ILM FUNCTION POINTERS (from working implementation)
 * ============================================================================ */

/* ILM type definitions */
typedef unsigned int t_ilm_uint;
typedef unsigned char t_ilm_bool;
typedef void* t_ilm_nativehandle;
typedef unsigned int t_ilm_surface;
typedef enum { ILM_PIXELFORMAT_RGBA_8888 = 0 } ilmPixelFormat;
typedef enum { 
    ILM_ERROR_NONE = 0,
    ILM_ERROR_INVALID_ARGUMENTS = 1,
    ILM_ERROR_TIMEOUT = 2,
    ILM_ERROR_RESOURCE_NOT_FOUND = 3,
    ILM_ERROR_RESOURCE_ALREADY_INUSE = 4,
    ILM_ERROR_NOT_IMPLEMENTED = 5,
    ILM_ERROR_UNEXPECTED_MESSAGE = 6,
    ILM_ERROR_UNKNOWN = 99
} t_ilm_errorTypes;

/* Function to convert ILM error codes to human-readable strings */
static const char* ilm_error_to_string(t_ilm_errorTypes error) {
    switch (error) {
        case ILM_ERROR_NONE: return "Success";
        case ILM_ERROR_INVALID_ARGUMENTS: return "Invalid arguments";
        case ILM_ERROR_TIMEOUT: return "Timeout";
        case ILM_ERROR_RESOURCE_NOT_FOUND: return "Resource not found";
        case ILM_ERROR_RESOURCE_ALREADY_INUSE: return "Resource already in use";
        case ILM_ERROR_NOT_IMPLEMENTED: return "Not implemented";
        case ILM_ERROR_UNEXPECTED_MESSAGE: return "Unexpected message";
        default: return "Unknown error";
    }
}

/* Global layer tracking for render order management */
static t_ilm_uint g_layer_list[10] = {0}; /* Track up to 10 layers */
static t_ilm_uint g_layer_count = 0;

/* Add layer to global list (avoids duplicates) */
static void add_layer_to_global_list(t_ilm_uint layer_id) {
    /* Check if layer already exists */
    for (t_ilm_uint i = 0; i < g_layer_count; i++) {
        if (g_layer_list[i] == layer_id) {
            printf("[EVO] Layer %u already in global render list\n", layer_id);
            return;
        }
    }
    
    /* Add new layer if space available */
    if (g_layer_count < 10) {
        g_layer_list[g_layer_count] = layer_id;
        g_layer_count++;
        printf("[EVO] Added layer %u to global render list (total: %u layers)\n", layer_id, g_layer_count);
    } else {
        printf("[EVO] Warning: Maximum layer count reached, cannot add layer %u\n", layer_id);
    }
}

/* ILM function pointers */
static t_ilm_errorTypes (*ilm_init_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_destroy_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_commitChanges_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_surfaceCreate_ptr)(t_ilm_nativehandle, t_ilm_uint, t_ilm_uint, ilmPixelFormat, t_ilm_surface*) = NULL;
static t_ilm_errorTypes (*ilm_layerCreateWithDimension_ptr)(t_ilm_uint*, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerAddSurface_ptr)(t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_surfaceSetVisibility_ptr)(t_ilm_uint, t_ilm_bool) = NULL;
static t_ilm_errorTypes (*ilm_layerSetVisibility_ptr)(t_ilm_uint, t_ilm_bool) = NULL;
static t_ilm_errorTypes (*ilm_surfaceSetDestinationRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_surfaceSetSourceRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerSetDestinationRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_displaySetRenderOrder_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint) = NULL;
static void (*ilm_getLayerIDsOnScreen_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint**) = NULL;
/* Layer lifecycle and ordering, needed so the LayerManager actually composites our window */
static t_ilm_errorTypes (*ilm_layerRemove_ptr)(t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerRemoveSurface_ptr)(t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerSetRenderOrder_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerSetSourceRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerGetVisibility_ptr)(t_ilm_uint, t_ilm_bool*) = NULL;
static t_ilm_errorTypes (*ilm_getSurfaceIDsOnLayer_ptr)(t_ilm_uint, int*, t_ilm_uint**) = NULL;
static t_ilm_errorTypes (*ilm_getScreenResolution_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint*) = NULL;
static t_ilm_errorTypes (*ilm_getSurfaceIDs_ptr)(int*, t_ilm_uint**) = NULL;

/* Foreign layers (e.g. the car HMI) whose destination rectangle we changed for a
 * docked layout; evo_display_hide() puts them back to full screen. */
#define EVO_MAX_FOREIGN_LAYERS 4
static t_ilm_uint g_foreign_layers[EVO_MAX_FOREIGN_LAYERS];
static int g_foreign_layer_count = 0;

/* Layer management for presentation mode */
static t_ilm_uint* g_hidden_layers = NULL;       /* Layers we've hidden for presentation mode */
static t_ilm_uint g_hidden_count = 0;           /* Number of hidden layers */
static t_ilm_bool g_presentation_mode = 0;       /* Are we in presentation mode? */

/* Display context structure */
/* Singleton display context (only one allowed per process) */
// Singleton display context (only one allowed per process)
// Display context structure (internal use only)
typedef struct {
    int width, height, x, y;
    uint32_t surface_id, layer_id;
    screen_context_t screen_ctx;
    screen_window_t screen_win;
    screen_buffer_t screen_buf;      /* buffer that will be posted next (one of screen_bufs) */
    char *screen_ptr;                /* STABLE shadow frame handed to apps (never a scan-out buffer) */
    screen_buffer_t screen_bufs[2];  /* real window buffers (double-buffered scan-out) */
    char *screen_ptrs[2];
    int screen_stride;
    int buffer_count;
    int current_buffer;
    int front_buffer;
    int visible;
    int ilm_available;
    int layer_created;               /* We created layer_id in the LayerManager and must remove it */
    t_ilm_uint* original_render_order;   /* Store original render order for cleanup */
    t_ilm_uint original_render_count;    /* Store original render count */
    char *software_framebuffer;      /* Software framebuffer for ILM-only mode */
    char error_message[256];
} evo_display_context_t;

// Singleton display context (only one allowed per process)
static evo_display_context_t *singleton_ctx = NULL;

// --------------------------------------------------------------------------
// Implementations for error and ILM functions
void clear_error(void* ctx) {
    evo_display_context_t* dctx = (evo_display_context_t*)ctx;
    if (dctx) dctx->error_message[0] = '\0';
}

void set_error(void* ctx, const char* msg) {
    evo_display_context_t* dctx = (evo_display_context_t*)ctx;
    if (dctx && msg) snprintf(dctx->error_message, sizeof(dctx->error_message), "%s", msg);
}

int load_ilm_functions(void) {
    if (!g_ilm_handle) return -1;
    ilm_init_ptr = dlsym(g_ilm_handle, "ilm_init");
    ilm_destroy_ptr = dlsym(g_ilm_handle, "ilm_destroy");
    ilm_commitChanges_ptr = dlsym(g_ilm_handle, "ilm_commitChanges");
    ilm_surfaceCreate_ptr = dlsym(g_ilm_handle, "ilm_surfaceCreate");
    ilm_layerCreateWithDimension_ptr = dlsym(g_ilm_handle, "ilm_layerCreateWithDimension");
    ilm_layerAddSurface_ptr = dlsym(g_ilm_handle, "ilm_layerAddSurface");
    ilm_surfaceSetVisibility_ptr = dlsym(g_ilm_handle, "ilm_surfaceSetVisibility");
    ilm_layerSetVisibility_ptr = dlsym(g_ilm_handle, "ilm_layerSetVisibility");
    ilm_surfaceSetDestinationRectangle_ptr = dlsym(g_ilm_handle, "ilm_surfaceSetDestinationRectangle");
    ilm_surfaceSetSourceRectangle_ptr = dlsym(g_ilm_handle, "ilm_surfaceSetSourceRectangle");
    ilm_layerSetDestinationRectangle_ptr = dlsym(g_ilm_handle, "ilm_layerSetDestinationRectangle");
    ilm_displaySetRenderOrder_ptr = dlsym(g_ilm_handle, "ilm_displaySetRenderOrder");
    
    /* Load the correct function for getting layer IDs on screen */
    ilm_getLayerIDsOnScreen_ptr = dlsym(g_ilm_handle, "ilm_getLayerIDsOnScreen");
    ilm_layerRemove_ptr = dlsym(g_ilm_handle, "ilm_layerRemove");
    ilm_layerRemoveSurface_ptr = dlsym(g_ilm_handle, "ilm_layerRemoveSurface");
    ilm_layerSetRenderOrder_ptr = dlsym(g_ilm_handle, "ilm_layerSetRenderOrder");
    ilm_layerSetSourceRectangle_ptr = dlsym(g_ilm_handle, "ilm_layerSetSourceRectangle");
    ilm_layerGetVisibility_ptr = dlsym(g_ilm_handle, "ilm_layerGetVisibility");
    ilm_getSurfaceIDsOnLayer_ptr = dlsym(g_ilm_handle, "ilm_getSurfaceIDsOnLayer");
    ilm_getScreenResolution_ptr = dlsym(g_ilm_handle, "ilm_getScreenResolution");
    ilm_getSurfaceIDs_ptr = dlsym(g_ilm_handle, "ilm_getSurfaceIDs");
    
    // Debug: Check if render order functions loaded
    if (ilm_displaySetRenderOrder_ptr && ilm_getLayerIDsOnScreen_ptr) {
        printf("[EVO] ✓ Render order functions loaded successfully\n");
    } else {
        printf("[EVO] Warning: Render order functions not available in ILM library\n");
        printf("[EVO]   ilm_displaySetRenderOrder_ptr: %p\n", ilm_displaySetRenderOrder_ptr);
        printf("[EVO]   ilm_getLayerIDsOnScreen_ptr: %p\n", ilm_getLayerIDsOnScreen_ptr);
    }
    
    // Check for required symbols
    if (!ilm_init_ptr || !ilm_destroy_ptr || !ilm_commitChanges_ptr || !ilm_surfaceCreate_ptr) {
        return -1;
    }
    return 0;
}

void setup_ilm_integration(void* ctx) {
    evo_display_context_t* dctx = (evo_display_context_t*)ctx;
    if (!dctx || !ilm_init_ptr) return;
    t_ilm_errorTypes err = ilm_init_ptr();
    if (err != ILM_ERROR_NONE) {
        set_error(dctx, "ILM initialization failed");
        dctx->ilm_available = 0;
    } else {
        dctx->ilm_available = 1;
    }
}
// --------------------------------------------------------------------------

// Display system lifecycle entry points
int evo_display_init(void) {
    g_display_initialized = 1;
    EVO_LOG_INFO("Display system initialized");
    return 0;
}

void evo_display_shutdown(void) {
    g_display_initialized = 0;
    EVO_LOG_INFO("Display system shutdown");
}

/* ============================================================================
 * LAYER MANAGEMENT UTILITIES (NEW IN V3.0)
 * ============================================================================ */

/**
 * Enable presentation mode - hide other layers to give our application focus
 */
int evo_display_enable_presentation_mode(evo_display_t* display) {
    if (!display) {
        printf("[EVO] enable_presentation_mode called with NULL display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (g_presentation_mode) {
        printf("[EVO] Presentation mode already enabled\n");
        return 0;
    }
    
    printf("[EVO] Enabling presentation mode - hiding other layers...\n");
    
    if (!ctx->ilm_available || !ilm_getLayerIDsOnScreen_ptr || !ilm_layerSetVisibility_ptr) {
        printf("[EVO] ILM functions not available for presentation mode\n");
        return -1;
    }
    
    /* Get all current layers on screen */
    t_ilm_uint* current_layers = NULL;
    t_ilm_uint current_count = 0;
    
    ilm_getLayerIDsOnScreen_ptr(0, &current_count, &current_layers);
    
    if (current_layers && current_count > 0) {
        printf("[EVO] Found %u layers on screen for presentation mode\n", current_count);
        
        /* Allocate storage for layers we'll hide */
        g_hidden_layers = malloc(current_count * sizeof(t_ilm_uint));
        if (!g_hidden_layers) {
            printf("[EVO] Failed to allocate memory for hidden layers list\n");
            free(current_layers);
            return -1;
        }
        
        g_hidden_count = 0;
        
        /* Hide all layers except our own */
        for (t_ilm_uint i = 0; i < current_count; i++) {
            if (current_layers[i] != ctx->layer_id) {
                /* Only touch layers that are visible now. Hidden ones must stay hidden
                 * when presentation mode ends, or the HMI comes back with every dormant
                 * layer switched on. */
                /* The target's ILM client writes a 4-byte, 4-byte-aligned boolean here;
                 * a plain t_ilm_bool on the stack faults with SIGBUS in getBool(). */
                t_ilm_uint was_visible = 1;
                if (ilm_layerGetVisibility_ptr &&
                    ilm_layerGetVisibility_ptr(current_layers[i], (t_ilm_bool*)&was_visible) == ILM_ERROR_NONE &&
                    (was_visible & 0xFF) == 0) {
                    continue;
                }
                /* Hide this layer */
                t_ilm_errorTypes rc = ilm_layerSetVisibility_ptr(current_layers[i], 0);
                if (rc == ILM_ERROR_NONE) {
                    /* Store this layer so we can restore it later */
                    g_hidden_layers[g_hidden_count] = current_layers[i];
                    g_hidden_count++;
                    printf("[EVO]   Hidden layer: %u\n", current_layers[i]);
                } else {
                    printf("[EVO]   Failed to hide layer %u (rc=%d: %s)\n", 
                           current_layers[i], rc, ilm_error_to_string(rc));
                }
            } else {
                printf("[EVO]   Keeping our layer visible: %u\n", current_layers[i]);
            }
        }
        
        /* Commit the visibility changes */
        if (ilm_commitChanges_ptr) {
            t_ilm_errorTypes rc = ilm_commitChanges_ptr();
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✅ Presentation mode enabled - %u layers hidden\n", g_hidden_count);
                g_presentation_mode = 1;
            } else {
                printf("[EVO] Warning: Failed to commit presentation mode changes (rc=%d: %s)\n", 
                       rc, ilm_error_to_string(rc));
                if (rc == ILM_ERROR_TIMEOUT) {
                    printf("[EVO] Timeout during commit - presentation mode may still be active\n");
                    g_presentation_mode = 1;  /* Assume it worked despite timeout */
                }
            }
        }
        
        free(current_layers);
    } else {
        printf("[EVO] No other layers found to hide for presentation mode\n");
        g_presentation_mode = 1;  /* Consider it enabled even if no layers to hide */
    }
    
    return g_presentation_mode ? 0 : -1;
}

/**
 * Disable presentation mode - restore visibility of previously hidden layers
 */
int evo_display_disable_presentation_mode(evo_display_t* display) {
    if (!display) {
        printf("[EVO] disable_presentation_mode called with NULL display\n");
        return -1;
    }
    
    if (!g_presentation_mode) {
        printf("[EVO] Presentation mode not currently enabled\n");
        return 0;
    }
    
    printf("[EVO] Disabling presentation mode - restoring %u hidden layers...\n", g_hidden_count);
    
    if (g_hidden_layers && g_hidden_count > 0 && ilm_layerSetVisibility_ptr) {
        /* Restore visibility of all hidden layers */
        for (t_ilm_uint i = 0; i < g_hidden_count; i++) {
            t_ilm_errorTypes rc = ilm_layerSetVisibility_ptr(g_hidden_layers[i], 1);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO]   Restored layer: %u\n", g_hidden_layers[i]);
            } else {
                printf("[EVO]   Failed to restore layer %u (rc=%d: %s)\n", 
                       g_hidden_layers[i], rc, ilm_error_to_string(rc));
            }
        }
        
        /* Commit the visibility changes */
        if (ilm_commitChanges_ptr) {
            t_ilm_errorTypes rc = ilm_commitChanges_ptr();
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✅ Presentation mode disabled - layers restored\n");
            } else {
                printf("[EVO] Warning: Failed to commit layer restoration (rc=%d: %s)\n", 
                       rc, ilm_error_to_string(rc));
                if (rc == ILM_ERROR_TIMEOUT) {
                    printf("[EVO] Timeout during commit - layers may still be restored\n");
                }
            }
        }
        
        /* Clean up */
        free(g_hidden_layers);
        g_hidden_layers = NULL;
        g_hidden_count = 0;
    }
    
    g_presentation_mode = 0;
    printf("[EVO] Presentation mode disabled\n");
    return 0;
}

/**
 * Check if presentation mode is currently active
 */
int evo_display_is_presentation_mode(void) {
    return g_presentation_mode;
}

/**
 * Raise OUR layer to the top of the display render order (rendered last = on top) so our overlay
 * composites OVER the OEM HMI WITHOUT hiding it - unlike presentation mode.  Every other layer is
 * preserved in its current relative order; only our layer moves to the top.  Lets a translucent
 * overlay (e.g. the launcher card) float over the still-visible, dimmed live HMI.  Idempotent;
 * call it whenever the HMI's compositor may have re-ordered layers (e.g. on each summon).
 */
int evo_display_raise_to_top(evo_display_t* display) {
    if (!display) return -1;
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    if (!ctx->ilm_available || !ilm_getLayerIDsOnScreen_ptr ||
        !ilm_displaySetRenderOrder_ptr || !ilm_commitChanges_ptr) {
        printf("[EVO] raise_to_top: render-order ILM functions unavailable\n");
        return -1;
    }
    t_ilm_uint *layers = NULL;
    t_ilm_uint count = 0;
    ilm_getLayerIDsOnScreen_ptr(0, &count, &layers);   /* returns void, like enable_presentation_mode */
    if (!layers || count == 0) {
        if (layers) free(layers);
        return -1;
    }
    t_ilm_uint *order = (t_ilm_uint*)malloc(count * sizeof(t_ilm_uint));
    if (!order) { free(layers); return -1; }
    t_ilm_uint n = 0;
    for (t_ilm_uint i = 0; i < count; i++) {
        if (layers[i] != ctx->layer_id) order[n++] = layers[i];   /* keep the others in order */
    }
    order[n++] = ctx->layer_id;                                   /* ours LAST == top of the stack */
    t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, order, n);
    if (rc == ILM_ERROR_NONE) {
        ilm_commitChanges_ptr();
        printf("[EVO] Raised layer %u to top of render order (%u layers)\n", ctx->layer_id, n);
    } else {
        printf("[EVO] raise_to_top: ilm_displaySetRenderOrder failed (rc=%d: %s)\n",
               rc, ilm_error_to_string(rc));
    }
    free(order); free(layers);
    return (rc == ILM_ERROR_NONE) ? 0 : -1;
}

/* ============================================================================
 * FRAMEBUFFER ACCESS (for text rendering)
 * ============================================================================ */

uint32_t* evo_display_get_framebuffer(evo_display_t* display) {
    if (!display) {
        printf("[EVO] evo_display_get_framebuffer - Invalid display\n");
        return NULL;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    // Return the screen buffer pointer if available (Screen+ILM mode)
    if (ctx->screen_ptr) {
        EVO_LOG_DEBUG("Returning screen buffer: %p", ctx->screen_ptr);
        return (uint32_t*)ctx->screen_ptr;
    }
    
    // Return software framebuffer if available (ILM-only mode)
    if (ctx->software_framebuffer) {
        EVO_LOG_DEBUG("Returning software framebuffer: %p", ctx->software_framebuffer);
        return (uint32_t*)ctx->software_framebuffer;
    }
    
    printf("[EVO] evo_display_get_framebuffer - No framebuffer available (screen_ptr=%p, software_framebuffer=%p)\n", 
           ctx->screen_ptr, ctx->software_framebuffer);
    return NULL;
}


// ...existing code...
/* ============================================================================
 * DISPLAY LIFECYCLE (DEPRECATED FUNCTIONS)
 * ============================================================================ */

evo_display_t* evo_display_create(int width, int height, int x, int y, uint32_t surface_id, uint32_t layer_id)
{
    evo_display_context_t *ctx = malloc(sizeof(evo_display_context_t));
    if (!ctx) {
        printf("[EVO] Failed to allocate display context\n");
        return NULL;
    }
    
    memset(ctx, 0, sizeof(evo_display_context_t));
    clear_error(ctx);
    
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->surface_id = surface_id;
    ctx->layer_id = layer_id;
    
    /* Variable for legacy Screen API calls (unused in clean ILM-only mode) */
    int rc = 0;
    ctx->buffer_count = 1;  /* Single buffer like working simple_test - get stable first */
    ctx->current_buffer = 0;  /* Single buffer always uses buffer 0 */
    ctx->front_buffer = 0;    /* Single buffer - same as current */
    ctx->visible = 0;
    ctx->ilm_available = 0;
    
    printf("[EVO] Creating display context: %dx%d at (%d,%d), surface=%u, layer=%u\n", 
           width, height, x, y, surface_id, layer_id);
    
    /* SCREEN + ILM INTEGRATION APPROACH: Follow working common library pattern */
    printf("[EVO] Using Screen + ILM integration approach (like working common library)...\n");
    
    /* Create Screen context and window (like working common library) */
    rc = screen_create_context(&ctx->screen_ctx, 0);
    if (rc != 0) {
        printf("[EVO] Failed to create screen context (rc=%d)\n", rc);
        free(ctx);
        return NULL;
    }
    printf("[EVO] ✓ Screen context created\n");
    
    rc = screen_create_window(&ctx->screen_win, ctx->screen_ctx);
    if (rc != 0) {
        printf("[EVO] Failed to create screen window (rc=%d)\n", rc);
        screen_destroy_context(ctx->screen_ctx);
        free(ctx);
        return NULL;
    }
    printf("[EVO] ✓ Screen window created\n");
    
    ctx->ilm_available = 1; /* Enable ILM integration with Screen */
    
    printf("[EVO] Screen API initialized - LayerManagerService will coordinate with Screen\n");
    
    /* Configure window properties - only if we have a Screen window */
    if (ctx->screen_win) {
        /* Set pixel format (CRITICAL: Use SCREEN_FORMAT_RGBA8888 like working libraries) */
        int format = SCREEN_FORMAT_RGBA8888;  /* Use constant like working display helper */
        int rc_format = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &format);
        if (rc_format != 0) {
            printf("[EVO] WARNING: Failed to set pixel format RGBA8888 (rc=%d)\n", rc_format);
        }
        
        /* Verify what format was actually set */
        int actual_format = -1;
        int rc_get = screen_get_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &actual_format);
        if (rc_get == 0) {
            printf("[EVO] Pixel format set to %d (requested RGBA8888), rc=%d\n", actual_format, rc_format);
        } else {
            printf("[EVO] Could not verify pixel format (rc=%d)\n", rc_get);
        }
        
        /* Set window size (CRITICAL - working library sets this) */
        int size[2] = {width, height};
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size);
        
        /* Set window position (CRITICAL - working library sets this) */
        int pos[2] = {x, y};
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, pos);
        
        /* Set buffer usage (like working common library) */
        int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);

        /* Do not block screen_post_window() on the display refresh. With a single
         * render buffer the post otherwise waits for the next vsync, and a 35 fps
         * game loop with about 22 ms of work per frame settles at 30 fps. */
        int swap_interval = 0;
        if (screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SWAP_INTERVAL, &swap_interval) != 0) {
            printf("[EVO] Warning: could not set swap interval 0\n");
        }
        
        /* CRITICAL: Set window initially invisible to prevent flashing garbage (like working library) */
        int visibility = 0;  /* Start hidden - ILM will control visibility */
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_VISIBLE, &visibility);
        
        /* Set window ID string (CRITICAL for ILM integration) */
        char window_id[32];
        snprintf(window_id, sizeof(window_id), "%u", ctx->surface_id);
        rc = screen_set_window_property_cv(ctx->screen_win, SCREEN_PROPERTY_ID_STRING, 
                                          strlen(window_id), window_id);
        if (rc != 0) {
            printf("[EVO] WARNING: Failed to set window ID '%s' (error %d) - may affect ILM integration\n", 
                   window_id, rc);
        } else {
            printf("[EVO] Window ID set to '%s' for ILM surface@window integration\n", window_id);
        }
        
        printf("[EVO] Screen window configured: %dx%d at (%d,%d) with ID='%s'\n", 
               width, height, x, y, window_id);
    } else {
        printf("[EVO] Skipping Screen window configuration - using ILM-only mode\n");
    }
    
    /* Create buffers - only if we have a Screen window */
    if (ctx->screen_win) {
        /* Create buffers BEFORE ILM setup (like working simple_test) */
        ctx->current_buffer = 0;
        ctx->front_buffer = 0;
        
        int buffer_rc = screen_create_window_buffers(ctx->screen_win, 2);  /* double-buffered: no half-drawn frames on screen */
        if (buffer_rc) {
            printf("[EVO] Failed to create window buffers (rc=%d) - falling back to ILM-only mode\n", buffer_rc);
            screen_destroy_window(ctx->screen_win);
            screen_destroy_context(ctx->screen_ctx);
            ctx->screen_win = NULL;
            ctx->screen_ctx = NULL;
            ctx->screen_buf = NULL;
            ctx->screen_ptr = NULL;
            ctx->ilm_available = 1; /* Force ILM-only mode */
        } else {
            /* Get buffer handle (single buffer like old working code) */
            buffer_rc = screen_get_window_property_pv(ctx->screen_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)ctx->screen_bufs);
            ctx->screen_buf = ctx->screen_bufs[0];
            if (buffer_rc) {
                printf("[EVO] Failed to get window render buffer (rc=%d) - falling back to ILM-only mode\n", buffer_rc);
                screen_destroy_window(ctx->screen_win);
                screen_destroy_context(ctx->screen_ctx);
                ctx->screen_win = NULL;
                ctx->screen_ctx = NULL;
                ctx->screen_buf = NULL;
                ctx->screen_ptr = NULL;
                ctx->ilm_available = 1; /* Force ILM-only mode */
            } else {
                /* Get buffer pointer (single pointer like old working code) */
                buffer_rc = screen_get_buffer_property_pv(ctx->screen_bufs[0], SCREEN_PROPERTY_POINTER, (void**)&ctx->screen_ptrs[0]);
                if (!buffer_rc) buffer_rc = screen_get_buffer_property_pv(ctx->screen_bufs[1], SCREEN_PROPERTY_POINTER, (void**)&ctx->screen_ptrs[1]);
                ctx->screen_ptr = ctx->screen_ptrs[0];   /* replaced by the shadow frame below */
                if (buffer_rc) {
                    printf("[EVO] Failed to get buffer pointer (rc=%d) - falling back to ILM-only mode\n", buffer_rc);
                    screen_destroy_window(ctx->screen_win);
                    screen_destroy_context(ctx->screen_ctx);
                    ctx->screen_win = NULL;
                    ctx->screen_ctx = NULL;
                    ctx->screen_buf = NULL;
                    ctx->screen_ptr = NULL;
                    ctx->ilm_available = 1; /* Force ILM-only mode */
                } else {
                    printf("[EVO] Buffer pointer: %p\n", ctx->screen_ptr);
                    
                    buffer_rc = screen_get_buffer_property_iv(ctx->screen_buf, SCREEN_PROPERTY_STRIDE, &ctx->screen_stride);
                    if (buffer_rc) {
                        printf("[EVO] Failed to get buffer stride (rc=%d) - continuing anyway\n", buffer_rc);
                        ctx->screen_stride = width * 4; /* Assume RGBA8888 */
                    }
                    
                    ctx->buffer_count = 2;
                    ctx->current_buffer = 0;
                    printf("[EVO] Created %d window buffers (double-buffered scan-out + shadow frame)\n", ctx->buffer_count);
                    printf("[EVO] Buffer stride: %d bytes\n", ctx->screen_stride);
                    
                    /* CRITICAL: Clear buffer to black immediately (like working common library) */
                    if (ctx->screen_ptr) {
                        printf("[EVO] Clearing buffer to black (working pattern)...\n");
                        for (int y = 0; y < ctx->height; y++) {
                            unsigned char *row = (unsigned char*)ctx->screen_ptr + y * ctx->screen_stride;
                            for (int x = 0; x < ctx->width; x++) {
                                row[x * 4 + 0] = 0;  /* R */
                                row[x * 4 + 1] = 0;  /* G */
                                row[x * 4 + 2] = 0;  /* B */
                                row[x * 4 + 3] = 255; /* A */
                            }
                        }
                        printf("[EVO] Buffer cleared to black successfully\n");
                        /* second scan-out buffer: same content */
                        if (ctx->screen_ptrs[1]) memcpy(ctx->screen_ptrs[1], ctx->screen_ptrs[0], (size_t)ctx->height * ctx->screen_stride);
                        /* stable shadow frame for the application (persists across presents) */
                        {
                            char *shadow = (char*)malloc((size_t)ctx->height * ctx->screen_stride);
                            if (shadow) {
                                memcpy(shadow, ctx->screen_ptrs[0], (size_t)ctx->height * ctx->screen_stride);
                                ctx->screen_ptr = shadow;
                                printf("[EVO] Shadow frame allocated (%d bytes)\n", ctx->height * ctx->screen_stride);
                            } else {
                                printf("[EVO] WARNING: shadow frame malloc failed - drawing directly into buffer 0\n");
                            }
                        }
                    }
                    
                    printf("[EVO] Screen window created with buffers\n");
                }
            }
        }
    } else {
        printf("[EVO] Skipping Screen buffer creation - using ILM-only mode\n");
        ctx->screen_buf = NULL;
        ctx->screen_ptr = NULL;
        ctx->screen_stride = 0;
    }
    
    /* Always try to load ILM library - it's the primary display mechanism now */
    ctx->ilm_available = 0;
    
    printf("Loading ILM library (primary display mechanism)...\n");
    
    /* Priority 1: cpp-ne version (optimal for embedded) */
    g_ilm_handle = dlopen("libilmClient.cpp-ne.so.1", RTLD_LAZY);
    if (g_ilm_handle) {
        printf("✓ Loaded ILM: libilmClient.cpp-ne.so.1 (embedded optimized)\n");
        ctx->ilm_available = 1;
    } else {
        /* Priority 2: gpp version (general purpose) */
        g_ilm_handle = dlopen("libilmClient.gpp.so.1", RTLD_LAZY);
        if (g_ilm_handle) {
            printf("✓ Loaded ILM: libilmClient.gpp.so.1 (general purpose)\n");
            ctx->ilm_available = 1;
        } else {
            /* Priority 3: Generic fallback */
            g_ilm_handle = dlopen("libilmClient.so.1", RTLD_LAZY);
            if (g_ilm_handle) {
                printf("✓ Loaded ILM: libilmClient.so.1 (generic)\n");
                ctx->ilm_available = 1;
            } else {
                printf("ERROR: ILM library not available - cannot create functional display\n");
                /* Clean up any Screen resources */
                if (ctx->screen_win) {
                    screen_destroy_window(ctx->screen_win);
                }
                if (ctx->screen_ctx) {
                    screen_destroy_context(ctx->screen_ctx);
                }
                free(ctx);
                return NULL;
            }
        }
    }
    
    /* Set up ILM integration - this is now the PRIMARY display mechanism */
    if (ctx->ilm_available) {
        printf("✓ ILM library loaded successfully\n");
        
        /* Load function pointers */
        if (load_ilm_functions() == 0) {
            /* Set up full ILM integration - works with or without Screen window */
            setup_ilm_integration(ctx);
        } else {
            printf("ERROR: Failed to load ILM function pointers - cannot create functional display\n");
            /* Clean up any Screen resources */
            if (ctx->screen_win) {
                screen_destroy_window(ctx->screen_win);
            }
            if (ctx->screen_ctx) {
                screen_destroy_context(ctx->screen_ctx);
            }
            if (g_ilm_handle) {
                dlclose(g_ilm_handle);
                g_ilm_handle = NULL;
            }
            free(ctx);
            return NULL;
        }
    }
    
    g_display_initialized = 1;
    printf("[EVO] Display context created successfully\n");
    
    return (evo_display_t*)ctx;
}

evo_display_t* evo_display_create_ex(const evo_display_config_t* config)
{
    // DEPRECATED in v3.0 - Use unified Cairo system instead
    printf("[EVO] WARNING: evo_display_create_ex() is deprecated in v3.0\n");
    printf("[EVO] Please use evo_cairo_unified_init() for graphics operations\n");
    return NULL;
}

void evo_display_destroy(evo_display_t* display)
{
    if (!display) {
        printf("[EVO] Display destroy called with NULL display\n");
        return;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    printf("[EVO] Destroying display context\n");
    
    /* Hide display first */
    if (ctx->visible) {
        evo_display_hide(display);
    }
    
    /* Destroy screen resources */
    if (ctx->screen_win) {
        screen_destroy_window(ctx->screen_win);
        printf("[EVO] Screen window destroyed\n");
    }
    
    if (ctx->screen_ctx) {
        screen_destroy_context(ctx->screen_ctx);
        printf("[EVO] Screen context destroyed\n");
    }
    
    /* Cleanup ILM if available */
    if (ctx->ilm_available && g_ilm_handle) {
        /* Call ilm_destroy() first to properly shutdown ILM threads */
        if (ilm_destroy_ptr) {
            printf("[EVO] Calling ilm_destroy() to shutdown ILM threads...\n");
            t_ilm_errorTypes rc = ilm_destroy_ptr();
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ ILM destroyed successfully\n");
            } else {
                printf("[EVO] Warning: ILM destroy failed (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
            }
        }
        
        /* Now it's safe to unload the library */
        dlclose(g_ilm_handle);
        g_ilm_handle = NULL;
        printf("[EVO] ILM library unloaded\n");
    }
    
    free(ctx);
    printf("[EVO] Display context freed\n");
}

/* ============================================================================
 * DISPLAY CONTROL (MIXED IMPLEMENTATION)
 * ============================================================================ */

int evo_display_show(evo_display_t* display)
{
    if (!display) {
        printf("[EVO] Display show called with NULL display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (ctx->visible) {
        printf("[EVO] Display already visible\n");
        return 0;
    }
    
    printf("[EVO] Making display visible...\n");
    
    if (ctx->ilm_available) {
        /* ILM available - call ILM functions to set visibility (like working common library) */
        printf("[EVO] Setting ILM surface and layer visibility...\n");
        
        /* The LayerManager only learns about a Screen window (and creates the ILM
         * surface named by its ID string) once the window has been posted. Post the
         * black buffer once now, while the window is still invisible, then wait for
         * surface_id to show up before attaching it to our layer. */
        if (ctx->screen_win && ctx->screen_buf) {
            int dirty_rects[4] = {0, 0, ctx->width, ctx->height};
            int prc = screen_post_window(ctx->screen_win, ctx->screen_buf, 1, dirty_rects, 0);
            screen_flush_context(ctx->screen_ctx, 0);
            printf("[EVO] Initial window post for LayerManager registration (rc=%d)\n", prc);
        }
        if (ilm_getSurfaceIDs_ptr) {
            int attempt;
            int registered = 0;
            for (attempt = 0; attempt < 30 && !registered; attempt++) {
                int count = 0;
                t_ilm_uint* ids = NULL;
                if (ilm_getSurfaceIDs_ptr(&count, &ids) == ILM_ERROR_NONE && ids) {
                    int i;
                    for (i = 0; i < count; i++) {
                        if (ids[i] == ctx->surface_id) {
                            registered = 1;
                        }
                    }
                    free(ids);
                }
                if (!registered) {
                    usleep(100000);
                }
            }
            if (registered) {
                printf("[EVO] ✓ Surface %u registered by LayerManager after %d ms\n", ctx->surface_id, attempt * 100);
            } else {
                printf("[EVO] Warning: surface %u not registered by LayerManager after %d ms\n", ctx->surface_id, attempt * 100);
            }
        }

        /* Create our layer first. The window is created with SCREEN_PROPERTY_VISIBLE=0
         * and the LayerManager only switches it on once its surface sits on an
         * existing, visible layer that is in the screen render order. Without this
         * step every call below is accepted but applies to a layer that does not
         * exist, and the panel stays blank. */
        if (ilm_layerCreateWithDimension_ptr) {
            t_ilm_uint lid = ctx->layer_id;
            t_ilm_errorTypes rc = ilm_layerCreateWithDimension_ptr(&lid, (t_ilm_uint)ctx->width, (t_ilm_uint)ctx->height);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Layer %u created (%dx%d)\n", ctx->layer_id, ctx->width, ctx->height);
            } else {
                printf("[EVO] Warning: ilm_layerCreateWithDimension(%u) rc=%d: %s - assuming it already exists\n",
                       ctx->layer_id, rc, ilm_error_to_string(rc));
            }
            ctx->layer_created = 1;   /* either way, we own it and remove it on hide */
            if (ilm_commitChanges_ptr) {
                ilm_commitChanges_ptr();
            }
        } else {
            printf("[EVO] Warning: ilm_layerCreateWithDimension not available - layer %u may not exist\n", ctx->layer_id);
        }
        if (ilm_layerSetDestinationRectangle_ptr) {
            ilm_layerSetDestinationRectangle_ptr(ctx->layer_id, 0, 0, ctx->width, ctx->height);
        }
        if (ilm_layerSetSourceRectangle_ptr) {
            ilm_layerSetSourceRectangle_ptr(ctx->layer_id, 0, 0, ctx->width, ctx->height);
        }

        /* Set surface visibility */
        if (ilm_surfaceSetVisibility_ptr) {
            t_ilm_errorTypes rc = ilm_surfaceSetVisibility_ptr(ctx->surface_id, 1);
            if (rc == 0) {  /* ILM_SUCCESS = 0 */
                printf("[EVO] ✓ Surface %u set to visible\n", ctx->surface_id);
            } else {
                printf("[EVO] Warning: Failed to set surface %u visibility (rc=%d)\n", ctx->surface_id, rc);
            }
        } else {
            printf("[EVO] Warning: ilm_surfaceSetVisibility not available\n");
        }
        
        /* Set layer visibility */
        if (ilm_layerSetVisibility_ptr) {
            t_ilm_errorTypes rc = ilm_layerSetVisibility_ptr(ctx->layer_id, 1);
            if (rc == 0) {  /* ILM_SUCCESS = 0 */
                printf("[EVO] ✓ Layer %u set to visible\n", ctx->layer_id);
            } else {
                printf("[EVO] Warning: Failed to set layer %u visibility (rc=%d)\n", ctx->layer_id, rc);
            }
        } else {
            printf("[EVO] Warning: ilm_layerSetVisibility not available\n");
        }
        
        /* Add surface to layer (CRITICAL MISSING STEP!) */
        if (ilm_layerAddSurface_ptr) {
            t_ilm_errorTypes rc = ilm_layerAddSurface_ptr(ctx->layer_id, ctx->surface_id);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Surface %u added to layer %u\n", ctx->surface_id, ctx->layer_id);
            } else {
                printf("[EVO] Warning: Failed to add surface %u to layer %u (rc=%d: %s)\n", 
                       ctx->surface_id, ctx->layer_id, rc, ilm_error_to_string(rc));
            }
        } else {
            printf("[EVO] Warning: ilm_layerAddSurface not available\n");
        }

        /* The surface must also be in the layer's own render order to be drawn */
        if (ilm_layerSetRenderOrder_ptr) {
            t_ilm_uint surfaces[1];
            surfaces[0] = ctx->surface_id;
            t_ilm_errorTypes rc = ilm_layerSetRenderOrder_ptr(ctx->layer_id, surfaces, 1);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Layer %u render order set to surface %u\n", ctx->layer_id, ctx->surface_id);
            } else {
                printf("[EVO] Warning: Failed to set layer %u render order (rc=%d: %s)\n",
                       ctx->layer_id, rc, ilm_error_to_string(rc));
            }
        }
        
        /* Configure surface rectangles for proper display */
        if (ilm_surfaceSetDestinationRectangle_ptr) {
            t_ilm_errorTypes rc = ilm_surfaceSetDestinationRectangle_ptr(ctx->surface_id, 0, 0, ctx->width, ctx->height);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Surface destination rectangle set: %ux%u\n", ctx->width, ctx->height);
            } else {
                printf("[EVO] Warning: Failed to set surface destination rectangle (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
            }
        }
        
        if (ilm_surfaceSetSourceRectangle_ptr) {
            t_ilm_errorTypes rc = ilm_surfaceSetSourceRectangle_ptr(ctx->surface_id, 0, 0, ctx->width, ctx->height);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Surface source rectangle set: %ux%u\n", ctx->width, ctx->height);
            } else {
                printf("[EVO] Warning: Failed to set surface source rectangle (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
            }
        }
        
        /* Add layer to render order (CRITICAL for visibility!) */
        printf("[EVO] Adding layer to display render order...\n");
        
        /* Add this layer to global accumulator */
        add_layer_to_global_list(ctx->layer_id);
        
        /* Simple approach: Just add our layer to render order without complex optimization */
        t_ilm_bool render_order_set = 0;  /* Track if we successfully set render order */
        
        if (ilm_displaySetRenderOrder_ptr && ilm_getLayerIDsOnScreen_ptr) {
            /* Get current layer IDs on screen using proper ILM API */
            t_ilm_uint* current_layers = NULL;
            t_ilm_uint current_count = 0;
            
            printf("[EVO] Reading current layer IDs from screen 0...\n");
            ilm_getLayerIDsOnScreen_ptr(0, &current_count, &current_layers);
            
            if (current_layers && current_count > 0) {
                printf("[EVO] Found %u existing layers on screen\n", current_count);
                
                /* Simple strategy: Add our layer on top, preserving existing order */
                t_ilm_uint* new_order = malloc((current_count + 1) * sizeof(t_ilm_uint));
                if (new_order) {
                    /* Put our layer FIRST and keep the existing order after it. On the
                     * head unit the first entry of the render order is drawn on top:
                     * appended last, our layer was covered by the HMI base layer wherever
                     * they overlapped (picture-in-picture showed nothing). Drop a stale
                     * copy of our own id if a previous run left one behind. */
                    t_ilm_uint n = 0;
                    new_order[n++] = ctx->layer_id;
                    for (t_ilm_uint i = 0; i < current_count; i++) {
                        if (current_layers[i] != ctx->layer_id) {
                            new_order[n++] = current_layers[i];
                        }
                    }
                    printf("[EVO] Adding layer %u at top position (total: %u layers)\n", ctx->layer_id, n);
                    
                    /* Set new render order */
                    t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, new_order, n);
                    if (rc == ILM_ERROR_NONE) {
                        printf("[EVO] ✅ Render order updated with %u total layers\n", n);
                        printf("[EVO] 💡 Use evo_display_enable_presentation_mode() to hide other layers if needed\n");
                        
                        /* Store original for cleanup */
                        ctx->original_render_order = current_layers;
                        ctx->original_render_count = current_count;
                        current_layers = NULL; /* Don't free, we're storing it */
                        render_order_set = 1;  /* Success! */
                    } else {
                        printf("[EVO] ❌ Failed to set render order (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
                    }
                    free(new_order);
                } else {
                    printf("[EVO] ❌ Failed to allocate memory for render order\n");
                }
                
                /* Free current_layers if we didn't store it */
                if (current_layers) {
                    free(current_layers);
                }
            } else {
                printf("[EVO] ⚠️ No existing layers found on screen (count=%u, layers=%p)\n", 
                       current_count, current_layers);
                /* Just add our layer */
                t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, &ctx->layer_id, 1);
                if (rc == ILM_ERROR_NONE) {
                    printf("[EVO] ✓ Single layer render order set\n");
                    render_order_set = 1;  /* Success! */
                } else {
                    printf("[EVO] Warning: Failed to set single layer render order (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
                }
            }
        } else if (ilm_displaySetRenderOrder_ptr && !render_order_set) {
            /* Only setter available - use global accumulator approach */
            printf("[EVO] ⚠️ Layer reading not available, using global accumulator\n");
            t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, g_layer_list, g_layer_count);
            if (rc == ILM_ERROR_NONE) {
                printf("[EVO] ✓ Render order set with %u layers (will commit with other changes): ", g_layer_count);
                for (t_ilm_uint i = 0; i < g_layer_count; i++) {
                    printf("%u ", g_layer_list[i]);
                }
                printf("\n");
                render_order_set = 1;  /* Success! */
            } else {
                printf("[EVO] Warning: Failed to set accumulated render order (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
            }
        } else if (!render_order_set) {
            printf("[EVO] Warning: Render order functions not available - layer may not be visible\n");
            printf("[EVO] Attempting alternative visibility approach...\n");
            
            /* Try setting layer destination rectangle for full display */
            if (ilm_layerSetDestinationRectangle_ptr) {
                t_ilm_errorTypes rc = ilm_layerSetDestinationRectangle_ptr(ctx->layer_id, 0, 0, ctx->width, ctx->height);
                if (rc == ILM_ERROR_NONE) {
                    printf("[EVO] ✓ Layer destination rectangle set for full display\n");
                } else {
                    printf("[EVO] Warning: Failed to set layer destination rectangle (rc=%d: %s)\n", rc, ilm_error_to_string(rc));
                }
            }
        }
        
        /* Commit changes with retry for first-boot race condition */
        if (ilm_commitChanges_ptr) {
            t_ilm_errorTypes rc;
            int retry_count = 0;
            const int max_retries = 3;
            
            do {
                rc = ilm_commitChanges_ptr();
                if (rc == ILM_ERROR_NONE) {
                    printf("[EVO] ✓ ILM changes committed successfully\n");
                    break;
                } else {
                    retry_count++;
                    printf("[EVO] Warning: Failed to commit ILM changes (rc=%d: %s) - retry %d/%d\n", 
                           rc, ilm_error_to_string(rc), retry_count, max_retries);
                    if (rc == ILM_ERROR_TIMEOUT) {
                        printf("[EVO] ILM timeout - display system may be busy, retrying...\n");
                        usleep(100000); // 100ms delay before retry
                    }
                }
            } while (retry_count < max_retries && rc == ILM_ERROR_TIMEOUT);
            
            if (rc != ILM_ERROR_NONE) {
                printf("[EVO] ILM timeout - display system may be busy, but surfaces/layers are set\n");
            }
        }
        
        /* Verify the LayerManager really has our surface on our layer. The surface
         * is created by the LayerManager from the window ID string, asynchronously,
         * so the first add can race the window creation; retry a few times. */
        if (ilm_getSurfaceIDsOnLayer_ptr && ilm_layerAddSurface_ptr && ilm_commitChanges_ptr) {
            int attempt;
            int found = 0;
            for (attempt = 0; attempt < 10 && !found; attempt++) {
                int count = 0;
                t_ilm_uint* ids = NULL;
                if (ilm_getSurfaceIDsOnLayer_ptr(ctx->layer_id, &count, &ids) == ILM_ERROR_NONE && ids) {
                    int i;
                    for (i = 0; i < count; i++) {
                        if (ids[i] == ctx->surface_id) {
                            found = 1;
                        }
                    }
                    free(ids);
                }
                if (found) {
                    printf("[EVO] ✓ Surface %u confirmed on layer %u (attempt %d)\n", ctx->surface_id, ctx->layer_id, attempt + 1);
                    break;
                }
                printf("[EVO] Surface %u not on layer %u yet - re-adding (attempt %d)\n", ctx->surface_id, ctx->layer_id, attempt + 1);
                usleep(100000);
                ilm_layerAddSurface_ptr(ctx->layer_id, ctx->surface_id);
                if (ilm_layerSetRenderOrder_ptr) {
                    t_ilm_uint surfaces[1];
                    surfaces[0] = ctx->surface_id;
                    ilm_layerSetRenderOrder_ptr(ctx->layer_id, surfaces, 1);
                }
                if (ilm_surfaceSetVisibility_ptr) {
                    ilm_surfaceSetVisibility_ptr(ctx->surface_id, 1);
                }
                ilm_commitChanges_ptr();
            }
            if (!found) {
                printf("[EVO] ❌ Surface %u never appeared on layer %u - window will not be composited\n", ctx->surface_id, ctx->layer_id);
            }
        }

        printf("[EVO] ILM visibility setup complete\n");
    } else {
        /* ILM not available - use Screen API directly (working library pattern) */
        printf("[EVO] ILM not available - using Screen API directly...\n");
        
        /* Set window visibility using Screen API */
        int visibility = 1;
        int rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_VISIBLE, &visibility);
        if (rc == 0) {
            printf("[EVO] ✓ Screen window visibility set to visible\n");
        } else {
            printf("[EVO] Warning: Failed to set screen window visibility (%d)\n", rc);
        }
        
        /* Post window content for Screen API mode */
        int dirty_rects[4] = {0, 0, ctx->width, ctx->height};
        rc = screen_post_window(ctx->screen_win, ctx->screen_buf, 1, dirty_rects, 0);
        if (rc) {
            set_error(ctx, "Failed to post window buffer");
            printf("[EVO] Error: %s (rc=%d)\n", ctx->error_message, rc);
            return -1;
        }
    }
    
    ctx->visible = 1;
    printf("[EVO] Display is now visible\n");
    return 0;
}

int evo_display_hide(evo_display_t* display)
{
    if (!display) {
        printf("[EVO] Display hide called with NULL display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (!ctx->visible) {
        printf("[EVO] Display already hidden\n");
        return 0;
    }
    
    printf("[EVO] Hiding display\n");
    
    /* Disable presentation mode if it's active */
    if (g_presentation_mode) {
        printf("[EVO] Disabling presentation mode before hiding display...\n");
        evo_display_disable_presentation_mode(display);
    }
    
    /* Put back any foreign layer (HMI) we resized for a docked layout */
    if (ctx->ilm_available && g_foreign_layer_count > 0 && ilm_layerSetDestinationRectangle_ptr) {
        t_ilm_uint sw = (t_ilm_uint)ctx->width, sh = (t_ilm_uint)ctx->height;
        if (ilm_getScreenResolution_ptr) {
            t_ilm_uint w = 0, h = 0;
            if (ilm_getScreenResolution_ptr(0, &w, &h) == ILM_ERROR_NONE && w > 0 && h > 0) {
                sw = w;
                sh = h;
            }
        }
        for (int i = 0; i < g_foreign_layer_count; i++) {
            if (ilm_layerSetSourceRectangle_ptr) {
                ilm_layerSetSourceRectangle_ptr(g_foreign_layers[i], 0, 0, sw, sh);
            }
            t_ilm_errorTypes rc = ilm_layerSetDestinationRectangle_ptr(g_foreign_layers[i], 0, 0, sw, sh);
            printf("[EVO] Foreign layer %u restored to %ux%u (rc=%d)\n", g_foreign_layers[i], sw, sh, rc);
        }
        g_foreign_layer_count = 0;
        if (ilm_commitChanges_ptr) {
            ilm_commitChanges_ptr();
        }
    }

    /* Take our surface off our layer and hide it */
    if (ctx->ilm_available) {
        if (ilm_surfaceSetVisibility_ptr) {
            ilm_surfaceSetVisibility_ptr(ctx->surface_id, 0);
        }
        if (ilm_layerRemoveSurface_ptr) {
            t_ilm_errorTypes rc = ilm_layerRemoveSurface_ptr(ctx->layer_id, ctx->surface_id);
            printf("[EVO] Surface %u removed from layer %u (rc=%d)\n", ctx->surface_id, ctx->layer_id, rc);
        }
    }

    /* Restore original render order if we modified it */
    if (ctx->original_render_order && ilm_displaySetRenderOrder_ptr) {
        printf("[EVO] Restoring original render order...\n");
        /* Never hand back an order that still names our layer (stale from an earlier run) */
        t_ilm_uint kept = 0;
        for (t_ilm_uint i = 0; i < ctx->original_render_count; i++) {
            if (ctx->original_render_order[i] != ctx->layer_id) {
                ctx->original_render_order[kept++] = ctx->original_render_order[i];
            }
        }
        ctx->original_render_count = kept;
        t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, ctx->original_render_order, ctx->original_render_count);
        if (rc == 0) {
            printf("[EVO] ✓ Original render order restored\n");
        } else {
            printf("[EVO] Warning: Failed to restore render order (rc=%d)\n", rc);
        }
        
        /* Commit the change */
        if (ilm_commitChanges_ptr) {
            ilm_commitChanges_ptr();
        }
        
        /* Free the stored render order */
        free(ctx->original_render_order);
        ctx->original_render_order = NULL;
        ctx->original_render_count = 0;
    }

    /* Remove the layer we created so nothing is left behind in the LayerManager */
    if (ctx->ilm_available && ctx->layer_created && ilm_layerRemove_ptr) {
        t_ilm_errorTypes rc = ilm_layerRemove_ptr(ctx->layer_id);
        printf("[EVO] Layer %u removed (rc=%d: %s)\n", ctx->layer_id, rc, ilm_error_to_string(rc));
        ctx->layer_created = 0;
        if (ilm_commitChanges_ptr) {
            ilm_commitChanges_ptr();
        }
    }
    
    ctx->visible = 0;
    printf("[EVO] Display is now hidden\n");
    return 0;
}

int evo_display_set_layer_geometry(evo_display_t* display,
                                   int src_x, int src_y, int src_w, int src_h,
                                   int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!display) {
        return -1;
    }
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    if (!ctx->ilm_available || !ilm_layerSetDestinationRectangle_ptr) {
        printf("[EVO] set_layer_geometry: ILM layer control not available\n");
        return -1;
    }
    if (ilm_layerSetSourceRectangle_ptr) {
        ilm_layerSetSourceRectangle_ptr(ctx->layer_id, src_x, src_y, src_w, src_h);
    }
    t_ilm_errorTypes rc = ilm_layerSetDestinationRectangle_ptr(ctx->layer_id, dst_x, dst_y, dst_w, dst_h);
    if (ilm_commitChanges_ptr) {
        ilm_commitChanges_ptr();
    }
    printf("[EVO] Layer %u geometry: source %d,%d %dx%d -> screen %d,%d %dx%d (rc=%d: %s)\n",
           ctx->layer_id, src_x, src_y, src_w, src_h, dst_x, dst_y, dst_w, dst_h, rc, ilm_error_to_string(rc));
    return rc == ILM_ERROR_NONE ? 0 : -1;
}

int evo_display_set_foreign_layer_rect(evo_display_t* display, uint32_t layer_id, int x, int y, int w, int h)
{
    if (!display) {
        return -1;
    }
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    if (!ctx->ilm_available || !ilm_layerSetDestinationRectangle_ptr) {
        printf("[EVO] set_foreign_layer_rect: ILM layer control not available\n");
        return -1;
    }
    int known = 0;
    for (int i = 0; i < g_foreign_layer_count; i++) {
        if (g_foreign_layers[i] == layer_id) {
            known = 1;
        }
    }
    if (!known) {
        if (g_foreign_layer_count >= EVO_MAX_FOREIGN_LAYERS) {
            printf("[EVO] set_foreign_layer_rect: too many foreign layers\n");
            return -1;
        }
        g_foreign_layers[g_foreign_layer_count++] = layer_id;
    }
    t_ilm_errorTypes rc = ilm_layerSetDestinationRectangle_ptr(layer_id, x, y, w, h);
    if (ilm_commitChanges_ptr) {
        ilm_commitChanges_ptr();
    }
    printf("[EVO] Foreign layer %u moved to %d,%d %dx%d (rc=%d: %s) - restored on hide\n",
           layer_id, x, y, w, h, rc, ilm_error_to_string(rc));
    return rc == ILM_ERROR_NONE ? 0 : -1;
}

int evo_display_move(evo_display_t* display, int x, int y)
{
    if (!display) {
        printf("[EVO] Display move called with NULL display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    printf("[EVO] Moving display to (%d,%d)\n", x, y);
    
    ctx->x = x;
    ctx->y = y;
    
    int position[2] = {x, y};
    int rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, position);
    if (rc) {
        set_error(ctx, "Failed to move window");
        printf("[EVO] Error: %s (rc=%d)\n", ctx->error_message, rc);
        return -1;
    }
    
    printf("[EVO] Display moved successfully\n");
    return 0;
}

int evo_display_resize(evo_display_t* display, int width, int height)
{
    if (!display) {
        printf("[EVO] Display resize called with NULL display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    printf("[EVO] Resizing display to %dx%d\n", width, height);
    
    ctx->width = width;
    ctx->height = height;
    
    int size[2] = {width, height};
    int rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size);
    if (rc) {
        set_error(ctx, "Failed to resize window");
        printf("[EVO] Error: %s (rc=%d)\n", ctx->error_message, rc);
        return -1;
    }
    
    printf("[EVO] Display resized successfully\n");
    return 0;
}

int evo_display_set_opacity(evo_display_t* display, float opacity)
{
    // DEPRECATED in v3.0 - Cairo handles transparency through RGBA values
    printf("[EVO] WARNING: evo_display_set_opacity() is deprecated in v3.0\n");
    printf("[EVO] Use RGBA color values in Cairo drawing functions instead\n");
    return -1;
}

/* ============================================================================
 * DISPLAY INFORMATION (PARTIAL IMPLEMENTATION)
 * ============================================================================ */

evo_display_config_t evo_display_get_config(evo_display_t* display)
{
    evo_display_config_t config = {0};
    
    if (!display) {
        printf("[EVO] WARNING: evo_display_get_config() called with NULL display\n");
        return config;
    }
    
    // DEPRECATED in v3.0 - Configuration is handled by unified Cairo system
    printf("[EVO] WARNING: evo_display_get_config() is deprecated in v3.0\n");
    printf("[EVO] Use evo_cairo_unified_get_width/height() for display info\n");
    
    return config;
}

int evo_display_is_visible(evo_display_t* display)
{
    if (!display) {
        printf("[EVO] Display is_visible called with NULL display\n");
        return 0;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    return ctx->visible;
}

int evo_display_get_bounds(evo_display_t* display, int* x, int* y, int* width, int* height)
{
    if (!display) {
        printf("[EVO] Display get_bounds called with NULL display\n");
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = 640;
        if (height) *height = 480;
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (x) *x = ctx->x;
    if (y) *y = ctx->y;
    if (width) *width = ctx->width;
    if (height) *height = ctx->height;
    
    printf("[EVO] Display bounds: %dx%d at (%d,%d)\n", ctx->width, ctx->height, ctx->x, ctx->y);
    return 0;
}

/* ============================================================================
 * INTEGRATION WITH GRAPHICS CONTEXT (DEPRECATED FUNCTIONS)
 * ============================================================================ */

int evo_context_attach_display(evo_context_t* ctx, evo_display_t* display)
{
    // DEPRECATED in v3.0 - No context/display separation in unified Cairo
    printf("[EVO] WARNING: evo_context_attach_display() is deprecated in v3.0\n");
    printf("[EVO] Unified Cairo system manages display automatically\n");
    return -1;
}

int evo_context_detach_display(evo_context_t* ctx)
{
    // DEPRECATED in v3.0 - No context/display separation in unified Cairo
    printf("[EVO] WARNING: evo_context_detach_display() is deprecated in v3.0\n");
    printf("[EVO] Use evo_cairo_unified_cleanup() to release display\n");
    return -1;
}

/* ============================================================================
 * BUFFER MANAGEMENT FUNCTIONS (Essential for Visual Output)
 * ============================================================================ */

/**
 * Update display by posting current buffer
 */
int evo_display_update(evo_display_t* display) {
    if (!display) {
        EVO_LOG_ERROR("Invalid display parameter");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (ctx->screen_win && ctx->screen_buf) {
        /* Screen+ILM mode - use Screen API posting */
        EVO_LOG_DEBUG("Using Screen+ILM mode for display update");
        int dirty_rects[4] = {0, 0, ctx->width, ctx->height};
        if (ctx->buffer_count == 2 && ctx->screen_ptrs[0] && ctx->screen_ptrs[1]
            && ctx->screen_ptr != ctx->screen_ptrs[0] && ctx->screen_ptr != ctx->screen_ptrs[1]) {
            /* copy the completed shadow frame into the buffer that is NOT on screen, then post it */
            int next = 1 - ctx->current_buffer;
            memcpy(ctx->screen_ptrs[next], ctx->screen_ptr, (size_t)ctx->height * ctx->screen_stride);
            ctx->screen_buf = ctx->screen_bufs[next];
            ctx->current_buffer = next;
        }
        int rc = screen_post_window(ctx->screen_win, ctx->screen_buf, 1, dirty_rects, 0);
        if (rc != 0) {
            EVO_LOG_WARN("Failed to post window buffer (rc=%d)", rc);
        } else {
            EVO_LOG_DEBUG("Screen window posted successfully");
        }
        
        /* Flush the context */
        rc = screen_flush_context(ctx->screen_ctx, 0);
        if (rc != 0) {
            EVO_LOG_WARN("Failed to flush context (rc=%d)", rc);
        } else {
            EVO_LOG_DEBUG("Screen context flushed successfully");
        }
        
        return 0;
    } else if (ctx->ilm_available && ctx->software_framebuffer) {
        /* ILM-only mode - framebuffer changes handled automatically */
        EVO_LOG_DEBUG("Using ILM-only mode for display update");
        return 0;
    } else {
        EVO_LOG_ERROR("No display mechanism available (ILM=%d, framebuffer=%p)", 
                     ctx->ilm_available, ctx->software_framebuffer);
        return -1;
    }
}

/**
 * Swap front and back buffers for double buffering
 */
/**
 * Direct drawing: hand out the scan-out buffer that is NOT on screen, so an app that
 * redraws only part of the frame (an emulator's picture) can write straight into it
 * instead of drawing into the shadow frame and having all of it copied on every
 * present. Returns NULL when the window is not double-buffered (use the shadow frame
 * and evo_display_swap_buffers() then).
 *
 * The two scan-out buffers alternate, so each one sees every OTHER frame: whatever
 * lies outside the area the app redraws has to be drawn into both of them (two
 * consecutive frames) before it can be left alone. Write only - this is graphics
 * memory and reading it back is slow.
 */
void *evo_display_direct_begin(evo_display_t* display, int *stride_bytes) {
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    if (!ctx || !ctx->screen_win || ctx->buffer_count != 2 || !ctx->screen_ptrs[0] || !ctx->screen_ptrs[1]) return NULL;
    if (ctx->screen_ptr == ctx->screen_ptrs[0] || ctx->screen_ptr == ctx->screen_ptrs[1]) return NULL;   /* no shadow frame */
    if (stride_bytes) *stride_bytes = ctx->screen_stride;
    return ctx->screen_ptrs[1 - ctx->current_buffer];
}

/**
 * Post the buffer evo_display_direct_begin() handed out. x/y/w/h is the area that
 * changed since this buffer was last on screen (a hint for the compositor).
 */
int evo_display_direct_post(evo_display_t* display, int x, int y, int w, int h) {
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    int next, rc, rect[4];
    if (!ctx || !ctx->screen_win || ctx->buffer_count != 2) return -1;
    next = 1 - ctx->current_buffer;
    rect[0] = x; rect[1] = y; rect[2] = x + w; rect[3] = y + h;
    ctx->screen_buf = ctx->screen_bufs[next];
    ctx->front_buffer = ctx->current_buffer = next;
    rc = screen_post_window(ctx->screen_win, ctx->screen_buf, 1, rect, 0);
    screen_flush_context(ctx->screen_ctx, 0);
    return rc;
}

int evo_display_swap_buffers(evo_display_t* display) {
    if (!display) {
        printf("[EVO] evo_display_swap_buffers - Invalid display\n");
        return -1;
    }
    
    evo_display_context_t *ctx = (evo_display_context_t*)display;
    
    if (ctx->buffer_count < 2) {
        /* Single buffering - just update display */
        return evo_display_update(display);
    }
    
    /* Double buffering - swap front and back buffers */
    ctx->front_buffer = ctx->current_buffer;  /* Make current buffer the front buffer */
    
    /* Update display with new front buffer */
    int rc = evo_display_update(display);
    if (rc != 0) {
        return rc;
    }
    
    /* Set up next back buffer for rendering */
    ctx->current_buffer = 1 - ctx->front_buffer;
    
    /* (No per-frame log line here: an app's stdout goes to a RAM-backed file in
     * /dev/shmem, and one line per frame grew it by megabytes per hour of play.) */
    return 0;
}

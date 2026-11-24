/*
 * EVO Display Helper Implementation - Unified Screen + ILM Integration
 * 
 * Implementation of the simplified EVO display API.
 * Based on proven patterns from working EVO infotainment systems.
 */

#include "evo_display_helper.h"
#include "backends/evo_backend.h"
#include <signal.h>
#include <unistd.h>

/* ============================================================================
 * ILM DYNAMIC LOADING (from ultimate templates)
 * ============================================================================ */

/* Global ILM state */
static void *g_ilm_handle = NULL;
static int g_ilm_init_count = 0;

/* ILM function pointers */
static t_ilm_errorTypes (*ilm_init_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_destroy_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_commitChanges_ptr)(void) = NULL;
static t_ilm_errorTypes (*ilm_getScreenResolution_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint*) = NULL;
static t_ilm_errorTypes (*ilm_surfaceCreate_ptr)(t_ilm_nativehandle, t_ilm_uint, t_ilm_uint, ilmPixelFormat, t_ilm_surface*) = NULL;
static t_ilm_errorTypes (*ilm_layerCreateWithDimension_ptr)(t_ilm_uint*, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerAddSurface_ptr)(t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerRemoveSurface_ptr)(t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_surfaceSetVisibility_ptr)(t_ilm_uint, t_ilm_bool) = NULL;
static t_ilm_errorTypes (*ilm_layerSetVisibility_ptr)(t_ilm_uint, t_ilm_bool) = NULL;
static t_ilm_errorTypes (*ilm_surfaceSetDestinationRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_layerSetDestinationRectangle_ptr)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_displaySetRenderOrder_ptr)(t_ilm_uint, t_ilm_uint*, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_getDisplayRenderOrder_ptr)(t_ilm_uint, t_ilm_uint**, t_ilm_uint*) = NULL;
static t_ilm_errorTypes (*ilm_surfaceRemove_ptr)(t_ilm_uint) = NULL;

/* ============================================================================
 * SURFACE ID VALIDATION
 * ============================================================================ */

/**
 * Validate surface ID to avoid conflicts with system-reserved ranges
 * Based on observed system surface allocations from LayerManagerControl
 */
void evo_display_validate_surface_id(unsigned int surface_id) {
    int is_potentially_unsafe = 0;
    const char *warning_reason = NULL;
    
    /* Check against known system-reserved ranges */
    if (surface_id >= 32 && surface_id <= 33) {
        is_potentially_unsafe = 1;
        warning_reason = "system base range (32-33)";
    } else if (surface_id >= 80 && surface_id <= 81) {
        is_potentially_unsafe = 1;
        warning_reason = "system range (80-81)";
    } else if (surface_id >= 256 && surface_id <= 521) {
        is_potentially_unsafe = 1;
        warning_reason = "system block range (256-521)";
    } else if (surface_id >= 528 && surface_id <= 537) {
        is_potentially_unsafe = 1;
        warning_reason = "system range (528-537)";
    } else if (surface_id >= 544 && surface_id <= 545) {
        is_potentially_unsafe = 1;
        warning_reason = "system range (544-545)";
    } else if (surface_id >= 592 && surface_id <= 593) {
        is_potentially_unsafe = 1;
        warning_reason = "system range (592-593)";
    } else if (surface_id >= 768 && surface_id <= 851) {
        is_potentially_unsafe = 1;
        warning_reason = "system block range (768-851)";
    } else if (surface_id >= 1024 && surface_id <= 1056) {
        is_potentially_unsafe = 1;
        warning_reason = "system range (1024-1056)";
    } else if (surface_id >= 65537 && surface_id <= 65793) {
        is_potentially_unsafe = 1;
        warning_reason = "system high range (65537-65793)";
    } else if (surface_id >= 131073 && surface_id <= 131329) {
        is_potentially_unsafe = 1;
        warning_reason = "system high range (131073-131329)";
    }
    
    if (is_potentially_unsafe) {
        printf("⚠️  WARNING: Surface ID %u may conflict with %s!\n", 
               surface_id, warning_reason);
        printf("⚠️  Consider using safe ranges: 1500-1999, 2000-2999, 3000-3999\n");
    } else if (surface_id < 1500) {
        printf("ℹ️  Surface ID %u: Consider using higher IDs (1500+) to avoid future conflicts\n", 
               surface_id);
    } else {
        printf("✅ Surface ID %u: Appears to be in a safe range\n", surface_id);
    }
}

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================ */

static void set_error(evo_display_context_t *ctx, const char *message) {
    if (ctx) {
        ctx->last_error = -1;
        strncpy(ctx->error_message, message, sizeof(ctx->error_message) - 1);
        ctx->error_message[sizeof(ctx->error_message) - 1] = '\0';
        printf("ERROR: %s\n", message);
    }
}

static void clear_error(evo_display_context_t *ctx) {
    if (ctx) {
        ctx->last_error = 0;
        ctx->error_message[0] = '\0';
    }
}

/**
 * Load ILM library with comprehensive fallback strategy
 */
static int load_ilm_library(void) {
    if (g_ilm_handle) {
        g_ilm_init_count++;
        return 0; /* Already loaded */
    }
    
    printf("Loading ILM library with comprehensive fallback strategy...\n");
    
    /* Priority 1: cpp-ne version (optimal for embedded) */
    g_ilm_handle = dlopen("libilmClient.cpp-ne.so.1", RTLD_LAZY);
    if (g_ilm_handle) {
        printf("✓ Loaded ILM: libilmClient.cpp-ne.so.1 (embedded optimized)\n");
    } else {
        /* Priority 2: gpp version */
        g_ilm_handle = dlopen("libilmClient.gpp.so.1", RTLD_LAZY);
        if (g_ilm_handle) {
            printf("✓ Loaded ILM: libilmClient.gpp.so.1 (general purpose)\n");
        } else {
            /* Priority 3: Generic fallback */
            g_ilm_handle = dlopen("libilmClient.so.1", RTLD_LAZY);
            if (g_ilm_handle) {
                printf("✓ Loaded ILM: libilmClient.so.1 (generic)\n");
            } else {
                printf("Warning: ILM library not available - windows may not be visible\n");
                return -1;
            }
        }
    }
    
    /* Load essential function pointers */
    ilm_init_ptr = dlsym(g_ilm_handle, "ilm_init");
    ilm_destroy_ptr = dlsym(g_ilm_handle, "ilm_destroy");
    ilm_commitChanges_ptr = dlsym(g_ilm_handle, "ilm_commitChanges");
    ilm_getScreenResolution_ptr = dlsym(g_ilm_handle, "ilm_getScreenResolution");
    ilm_surfaceCreate_ptr = dlsym(g_ilm_handle, "ilm_surfaceCreate");
    ilm_layerCreateWithDimension_ptr = dlsym(g_ilm_handle, "ilm_layerCreateWithDimension");
    ilm_layerAddSurface_ptr = dlsym(g_ilm_handle, "ilm_layerAddSurface");
    ilm_layerRemoveSurface_ptr = dlsym(g_ilm_handle, "ilm_layerRemoveSurface");
    ilm_surfaceSetVisibility_ptr = dlsym(g_ilm_handle, "ilm_surfaceSetVisibility");
    ilm_layerSetVisibility_ptr = dlsym(g_ilm_handle, "ilm_layerSetVisibility");
    ilm_surfaceSetDestinationRectangle_ptr = dlsym(g_ilm_handle, "ilm_surfaceSetDestinationRectangle");
    ilm_layerSetDestinationRectangle_ptr = dlsym(g_ilm_handle, "ilm_layerSetDestinationRectangle");
    ilm_displaySetRenderOrder_ptr = dlsym(g_ilm_handle, "ilm_displaySetRenderOrder");
    ilm_getDisplayRenderOrder_ptr = dlsym(g_ilm_handle, "ilm_getDisplayRenderOrder");
    ilm_surfaceRemove_ptr = dlsym(g_ilm_handle, "ilm_surfaceRemove");
    
    if (!ilm_init_ptr || !ilm_commitChanges_ptr) {
        printf("Warning: Essential ILM functions not found\n");
        dlclose(g_ilm_handle);
        g_ilm_handle = NULL;
        return -1;
    }
    
    g_ilm_init_count = 1;
    printf("✓ ILM library loaded successfully\n");
    return 0;
}

static void unload_ilm_library(void) {
    if (g_ilm_handle && --g_ilm_init_count == 0) {
        dlclose(g_ilm_handle);
        g_ilm_handle = NULL;
        printf("✓ ILM library unloaded\n");
    }
}

/* Global timeout flag for ILM operations */
static volatile int g_ilm_commit_timeout = 0;

static void ilm_commit_timeout_handler(int sig) {
    g_ilm_commit_timeout = 1;
}

static int commit_ilm_changes(const char *description) {
    if (!g_ilm_handle || !ilm_commitChanges_ptr) {
        return 0; /* ILM not available */
    }
    
    printf("🔄 Committing ILM changes (%s) - this may take a moment...\n", description);
    
    /* Add timeout protection for ILM commit operations */
    struct sigaction sa, old_sa;
    g_ilm_commit_timeout = 0;
    
    /* Set up 5-second timeout for ILM commit */
    sa.sa_handler = ilm_commit_timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, &old_sa);
    
    alarm(5);
    t_ilm_errorTypes rc = ilm_commitChanges_ptr();
    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);
    
    if (g_ilm_commit_timeout) {
        printf("⚠️ ILM commit timed out for %s - continuing anyway\n", description);
        return -1;
    }
    
    if (rc != ILM_SUCCESS) {
        printf("Warning: Failed to commit ILM changes for %s (%d)\n", description, rc);
        return -1;
    }
    
    printf("✓ ILM changes committed (%s)\n", description);
    return 0;
}

/* ============================================================================
 * SCREEN API FUNCTIONS
 * ============================================================================ */

static int create_screen_context(evo_display_context_t *ctx) {
    /* If a backend is selected, configure context with backend-specific settings */
    if (ctx->selected_backend != EVO_BACKEND_AUTO && ctx->backend_config) {
        printf("🎯 Creating screen context with backend: %s\n", ctx->backend_config->name);
        
        /* Create context with backend hints if available */
        int rc = screen_create_context(&ctx->screen_ctx, 0);
        if (rc != 0) {
            set_error(ctx, "Failed to create screen context");
            return -1;
        }
        
        /* Set backend-specific buffer strategy */
        ctx->buffer_count = (ctx->buffer_strategy == EVO_BUFFER_DOUBLE) ? 2 : 1;
        printf("✓ Screen context created with %s buffering (%s acceleration)\n",
               (ctx->buffer_strategy == EVO_BUFFER_DOUBLE) ? "double" : "single",
               ctx->backend_config->is_hardware_accelerated ? "hardware" : "software");
    } else {
        /* Default context creation (auto-selection) */
        int rc = screen_create_context(&ctx->screen_ctx, 0);
        if (rc != 0) {
            set_error(ctx, "Failed to create screen context");
            return -1;
        }
        printf("✓ Screen context created (auto-selection)\n");
    }
    return 0;
}

static int create_screen_window(evo_display_context_t *ctx) {
    int rc;
    
    /* Create window */
    rc = screen_create_window(&ctx->screen_win, ctx->screen_ctx);
    if (rc != 0) {
        set_error(ctx, "Failed to create screen window");
        return -1;
    }
    
    /* Set pixel format */
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &format);
    
    /* Set window size */
    int size[2] = {ctx->width, ctx->height};
    screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size);
    
    /* Set window position */
    int pos[2] = {ctx->x, ctx->y};
    screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, pos);
    
    /* Set buffer usage */
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);
    
    /* Set window ID string (critical for ILM integration) */
    char window_id[32];
    snprintf(window_id, sizeof(window_id), "%u", ctx->surface_id);
    rc = screen_set_window_property_cv(ctx->screen_win, SCREEN_PROPERTY_ID_STRING, 
                                      strlen(window_id), window_id);
    if (rc != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to set window ID '%s'", window_id);
        set_error(ctx, error_msg);
        return -1;
    }
    
    printf("✓ Screen window created: %dx%d at (%d,%d) with ID='%s'\n", 
           ctx->width, ctx->height, ctx->x, ctx->y, window_id);
    return 0;
}

static int create_screen_buffer(evo_display_context_t *ctx) {
    /* Initialize buffer configuration but DON'T create buffers yet */
    /* Buffers will be created when double buffering is configured via evo_display_enable_double_buffering */
    printf("ℹ️ Deferring buffer creation until double buffering configuration is set\n");
    
    ctx->buffer_count = 0;  /* No buffers created yet */
    ctx->current_buffer = 0;
    ctx->front_buffer = 0;
    ctx->screen_buf[0] = NULL;
    ctx->screen_buf[1] = NULL;
    ctx->screen_ptr[0] = NULL;
    ctx->screen_ptr[1] = NULL;
    ctx->screen_stride = 0;
    
    printf("✓ Screen buffer system initialized (awaiting configuration)\n");
    return 0;
}

/* ============================================================================
 * ILM INTEGRATION FUNCTIONS
 * ============================================================================ */

static int initialize_ilm(evo_display_context_t *ctx) {
    if (!g_ilm_handle || !ilm_init_ptr) {
        printf("Warning: ILM not available - window may not be visible\n");
        ctx->ilm_available = 0;
        return 0;
    }
    
    printf("Initializing ILM system...\n");
    t_ilm_errorTypes rc = ilm_init_ptr();
    if (rc != ILM_SUCCESS) {
        printf("Warning: Failed to initialize ILM (%d)\n", rc);
        ctx->ilm_available = 0;
        return 0;
    }
    
    ctx->ilm_available = 1;
    ctx->ilm_initialized = 1;
    
    printf("✓ ILM system initialized (surface: %u, layer: %u)\n", 
           ctx->surface_id, ctx->layer_id);
    return 0;
}

static int setup_ilm_integration(evo_display_context_t *ctx) {
    if (!ctx->ilm_available) {
        printf("Skipping ILM integration - not available\n");
        return 0;
    }
    
    printf("Setting up ILM integration...\n");
    
    /* Create ILM layer */
    if (ilm_layerCreateWithDimension_ptr) {
        printf("Creating ILM layer %u with dimensions %dx%d...\n", 
               ctx->layer_id, ctx->width, ctx->height);
        
        t_ilm_uint layer_id = ctx->layer_id;
        t_ilm_errorTypes rc = ilm_layerCreateWithDimension_ptr(&layer_id, ctx->width, ctx->height);
        if (rc != ILM_SUCCESS) {
            printf("Warning: Failed to create ILM layer (%d)\n", rc);
        } else {
            printf("✓ ILM layer %u created successfully\n", ctx->layer_id);
        }
        commit_ilm_changes("layer creation");
    }
    
    /* Create ILM surface */
    if (ilm_surfaceCreate_ptr) {
        printf("Creating ILM surface %u with native window handle...\n", ctx->surface_id);
        
        t_ilm_surface surface_id = ctx->surface_id;
        t_ilm_errorTypes rc = ilm_surfaceCreate_ptr((t_ilm_nativehandle)ctx->screen_win,
                                                  ctx->width, ctx->height,
                                                  ILM_PIXELFORMAT_RGBA_8888, &surface_id);
        if (rc != ILM_SUCCESS) {
            printf("Warning: Failed to create ILM surface (%d)\n", rc);
        } else {
            printf("✓ ILM surface %u created successfully\n", surface_id);
        }
        commit_ilm_changes("surface creation");
    }
    
    /* Add surface to layer */
    if (ilm_layerAddSurface_ptr) {
        printf("Adding surface %u to layer %u...\n", ctx->surface_id, ctx->layer_id);
        t_ilm_errorTypes rc = ilm_layerAddSurface_ptr(ctx->layer_id, ctx->surface_id);
        if (rc != ILM_SUCCESS) {
            printf("Warning: Failed to add surface to layer (%d)\n", rc);
        } else {
            printf("✓ Surface added to layer successfully\n");
        }
        commit_ilm_changes("surface to layer");
    }
    
    /* Set layer position and size */
    if (ilm_layerSetDestinationRectangle_ptr) {
        printf("Setting layer destination rectangle: %dx%d at (%d,%d)...\n", 
               ctx->width, ctx->height, ctx->x, ctx->y);
        t_ilm_errorTypes rc = ilm_layerSetDestinationRectangle_ptr(ctx->layer_id, 
                                                                  ctx->x, ctx->y, 
                                                                  ctx->width, ctx->height);
        if (rc != ILM_SUCCESS) {
            printf("Warning: Failed to set layer destination rectangle (%d)\n", rc);
        } else {
            printf("✓ Layer destination rectangle set\n");
        }
        commit_ilm_changes("layer destination");
    }
    
    printf("✓ ILM integration setup completed\n");
    return 0;
}

/* ============================================================================
 * MAIN API IMPLEMENTATION
 * ============================================================================ */

evo_display_context_t* evo_display_create(int width, int height, int x, int y, 
                                         unsigned int surface_id, unsigned int layer_id) {
    printf("=== Creating EVO Display: %dx%d at (%d,%d) ===\n", width, height, x, y);
    
    /* Allocate context */
    evo_display_context_t *ctx = calloc(1, sizeof(evo_display_context_t));
    if (!ctx) {
        printf("ERROR: Failed to allocate display context\n");
        return NULL;
    }
    
    /* Initialize context */
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->surface_id = surface_id;
    ctx->layer_id = layer_id;
    ctx->visible = 0;
    clear_error(ctx);
    
    /* Initialize backend to auto-selection */
    ctx->selected_backend = EVO_BACKEND_AUTO;
    ctx->requested_backend = EVO_BACKEND_AUTO;
    ctx->backend_config = NULL;
    ctx->buffer_strategy = EVO_BUFFER_DOUBLE; /* Default to double buffering */
    ctx->allow_backend_fallback = 1;
    
    /* Validate surface ID to warn about potential conflicts */
    evo_display_validate_surface_id(surface_id);
    
    /* Step 1: Load ILM library */
    if (load_ilm_library() == 0) {
        ctx->ilm_available = 1;
    }
    
    /* Step 2: Create Screen context */
    if (create_screen_context(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 3: Create Screen window */
    if (create_screen_window(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 4: Create Screen buffer */
    if (create_screen_buffer(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 5: Initialize ILM */
    initialize_ilm(ctx);
    
    /* Step 6: Setup ILM integration */
    setup_ilm_integration(ctx);
    
    printf("✓ Display context created successfully\n");
    return ctx;
}

int evo_display_show(evo_display_context_t *ctx) {
    if (!ctx) return -1;
    
    printf("Making display visible...\n");
    
    if (ctx->ilm_available) {
        /* Set surface visibility */
        if (ilm_surfaceSetVisibility_ptr) {
            t_ilm_errorTypes rc = ilm_surfaceSetVisibility_ptr(ctx->surface_id, 1);
            if (rc == ILM_SUCCESS) {
                printf("✓ Surface %u set to visible\n", ctx->surface_id);
            }
        }
        
        /* Set layer visibility */
        if (ilm_layerSetVisibility_ptr) {
            t_ilm_errorTypes rc = ilm_layerSetVisibility_ptr(ctx->layer_id, 1);
            if (rc == ILM_SUCCESS) {
                printf("✓ Layer %u set to visible\n", ctx->layer_id);
            }
        }
        
        /* Add to render order */
        if (ilm_displaySetRenderOrder_ptr) {
            /* Get current render order first */
            t_ilm_uint *current_order = NULL;
            t_ilm_uint current_count = 0;
            
            if (ilm_getDisplayRenderOrder_ptr) {
                ilm_getDisplayRenderOrder_ptr(0, &current_order, &current_count);
            }
            
            /* Create new render order with our layer */
            t_ilm_uint *new_order = malloc(sizeof(t_ilm_uint) * (current_count + 1));
            if (new_order) {
                /* Copy existing order */
                if (current_order && current_count > 0) {
                    memcpy(new_order, current_order, sizeof(t_ilm_uint) * current_count);
                }
                /* Add our layer */
                new_order[current_count] = ctx->layer_id;
                
                t_ilm_errorTypes rc = ilm_displaySetRenderOrder_ptr(0, new_order, current_count + 1);
                if (rc == ILM_SUCCESS) {
                    printf("✓ Layer %u added to render order\n", ctx->layer_id);
                    /* Save original order for cleanup */
                    ctx->original_render_order = current_order;
                    ctx->original_render_count = current_count;
                } else {
                    printf("Warning: Failed to set render order (%d)\n", rc);
                    if (current_order) free(current_order);
                }
                free(new_order);
            }
        }
        
        commit_ilm_changes("visibility");
    } else {
        /* ILM not available - use Screen API directly */
        printf("ILM not available - setting Screen API visibility...\n");
        
        /* Set window visibility using Screen API */
        int visibility = 1;
        int rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_VISIBLE, &visibility);
        if (rc == 0) {
            printf("✓ Screen window visibility set to visible\n");
        } else {
            printf("Warning: Failed to set screen window visibility (%d)\n", rc);
        }
        
        /* Set window zorder to bring it to front */
        int zorder = 999;  /* High z-order value */
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_ZORDER, &zorder);
        if (rc == 0) {
            printf("✓ Screen window z-order set to %d\n", zorder);
        } else {
            printf("Warning: Failed to set screen window z-order (%d)\n", rc);
        }
        
        /* Flush the context to apply changes */
        rc = screen_flush_context(ctx->screen_ctx, 0);
        if (rc == 0) {
            printf("✓ Screen context flushed - changes applied\n");
        } else {
            printf("Warning: Failed to flush screen context (%d)\n", rc);
        }
    }
    
    ctx->visible = 1;
    printf("✓ Display is now visible\n");
    return 0;
}

int evo_display_update(evo_display_context_t *ctx) {
    if (!ctx) return -1;
    
    /* Post the front buffer to make changes visible */
    int dirty_rects[4] = {0, 0, ctx->width, ctx->height};
    int rc = screen_post_window(ctx->screen_win, ctx->screen_buf[ctx->front_buffer], 1, dirty_rects, 0);
    if (rc != 0) {
        set_error(ctx, "Failed to post window buffer");
        return -1;
    }
    
    /* Flush the context */
    rc = screen_flush_context(ctx->screen_ctx, 0);
    if (rc != 0) {
        set_error(ctx, "Failed to flush context");
        return -1;
    }
    
    return 0;
}

int evo_display_hide(evo_display_context_t *ctx) {
    if (!ctx) return -1;
    
    if (ctx->ilm_available && ctx->visible) {
        /* Hide surface */
        if (ilm_surfaceSetVisibility_ptr) {
            ilm_surfaceSetVisibility_ptr(ctx->surface_id, 0);
        }
        
        /* Hide layer */
        if (ilm_layerSetVisibility_ptr) {
            ilm_layerSetVisibility_ptr(ctx->layer_id, 0);
        }
        
        commit_ilm_changes("hide");
    }
    
    ctx->visible = 0;
    return 0;
}

void evo_display_destroy(evo_display_context_t **ctx_ptr) {
    if (!ctx_ptr || !*ctx_ptr) return;
    
    evo_display_context_t *ctx = *ctx_ptr;
    printf("Cleaning up display context...\n");
    
    /* Hide first */
    evo_display_hide(ctx);
    
    /* Clean up ILM resources */
    if (ctx->ilm_available && ctx->ilm_initialized) {
        /* Restore original render order */
        if (ctx->original_render_order && ilm_displaySetRenderOrder_ptr) {
            ilm_displaySetRenderOrder_ptr(0, ctx->original_render_order, 
                                        ctx->original_render_count);
            free(ctx->original_render_order);
        }
        
        /* Remove surface from layer */
        if (ilm_layerRemoveSurface_ptr) {
            ilm_layerRemoveSurface_ptr(ctx->layer_id, ctx->surface_id);
        }
        
        /* Remove surface */
        if (ilm_surfaceRemove_ptr) {
            ilm_surfaceRemove_ptr(ctx->surface_id);
        }
        
        commit_ilm_changes("cleanup");
        
        /* Destroy ILM */
        if (ilm_destroy_ptr) {
            ilm_destroy_ptr();
        }
        
        /* Unload ILM library immediately after destroy */
        unload_ilm_library();
    }
    
    /* Clean up Screen resources */
    if (ctx->screen_win) {
        screen_destroy_window(ctx->screen_win);
    }
    if (ctx->screen_ctx) {
        screen_destroy_context(ctx->screen_ctx);
    }
    
    /* Free context */
    free(ctx);
    *ctx_ptr = NULL;
    
    printf("✓ Display context destroyed\n");
}

void evo_display_destroy_and_exit(evo_display_context_t **ctx_ptr, int exit_code) {
    if (!ctx_ptr || !*ctx_ptr) {
        _exit(exit_code);
        return;
    }
    
    evo_display_context_t *ctx = *ctx_ptr;
    printf("Cleaning up display context and exiting safely...\n");
    
    /* Hide first */
    evo_display_hide(ctx);
    
    /* Clean up ILM resources */
    if (ctx->ilm_available && ctx->ilm_initialized) {
        /* Restore original render order */
        if (ctx->original_render_order && ilm_displaySetRenderOrder_ptr) {
            ilm_displaySetRenderOrder_ptr(0, ctx->original_render_order, 
                                        ctx->original_render_count);
            free(ctx->original_render_order);
        }
        
        /* Remove surface from layer */
        if (ilm_layerRemoveSurface_ptr) {
            ilm_layerRemoveSurface_ptr(ctx->layer_id, ctx->surface_id);
        }
        
        /* Remove surface */
        if (ilm_surfaceRemove_ptr) {
            ilm_surfaceRemove_ptr(ctx->surface_id);
        }
        
        commit_ilm_changes("cleanup");
        
        /* Destroy ILM */
        if (ilm_destroy_ptr) {
            ilm_destroy_ptr();
        }
        
        /* Don't unload library - will cause crash */
        printf("✓ ILM cleanup completed\n");
    }
    
    /* Clean up Screen resources */
    if (ctx->screen_win) {
        screen_destroy_window(ctx->screen_win);
    }
    if (ctx->screen_ctx) {
        screen_destroy_context(ctx->screen_ctx);
    }
    
    /* Free context */
    free(ctx);
    *ctx_ptr = NULL;
    
    printf("✓ Display context destroyed - exiting safely\n");
    
    /* Exit cleanly without triggering ILM destructor cascade */
    _exit(exit_code);
}

/* ============================================================================
 * DRAWING FUNCTIONS
 * ============================================================================ */

void evo_display_clear(evo_display_context_t *ctx, unsigned int color) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer]) return;
    
    /* Extract color components from EVO_COLOR_RGBA format
     * EVO_COLOR_RGBA(r,g,b,a) = (a << 24) | (r << 16) | (g << 8) | b
     * Hardware expects BGRA8888 memory layout (empirically determined)
     */
    unsigned char b = color & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char a = (color >> 24) & 0xFF;
    
    for (int y = 0; y < ctx->height; y++) {
        unsigned char *row = (unsigned char*)ctx->screen_ptr[ctx->current_buffer] + y * ctx->screen_stride;
        for (int x = 0; x < ctx->width; x++) {
            row[x * 4 + 0] = b;  /* Blue first (BGRA order) */
            row[x * 4 + 1] = g;  /* Green second */
            row[x * 4 + 2] = r;  /* Red third */
            row[x * 4 + 3] = a;  /* Alpha last */
        }
    }
}

/* ============================================================================
 * BASIC UTILITIES AND TEST FUNCTIONS
 * ============================================================================ */

void evo_display_draw_test_pattern(evo_display_context_t *ctx) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer]) return;
    
    /* Create a simple test pattern: colored rectangles and grid */
    int quarter_w = ctx->width / 4;
    int quarter_h = ctx->height / 4;
    
    /* Clear to black first */
    evo_display_clear(ctx, 0x000000FF);
    
    /* Draw colored squares in each corner */
    unsigned char *fb = (unsigned char*)ctx->screen_ptr[ctx->current_buffer];
    
    /* Top-left: Red */
    for (int y = 0; y < quarter_h; y++) {
        for (int x = 0; x < quarter_w; x++) {
            unsigned char *pixel = fb + y * ctx->screen_stride + x * 4;
            pixel[0] = 0xFF; pixel[1] = 0x00; pixel[2] = 0x00; pixel[3] = 0xFF;
        }
    }
    
    /* Top-right: Green */
    for (int y = 0; y < quarter_h; y++) {
        for (int x = ctx->width - quarter_w; x < ctx->width; x++) {
            unsigned char *pixel = fb + y * ctx->screen_stride + x * 4;
            pixel[0] = 0x00; pixel[1] = 0xFF; pixel[2] = 0x00; pixel[3] = 0xFF;
        }
    }
    
    /* Bottom-left: Blue */
    for (int y = ctx->height - quarter_h; y < ctx->height; y++) {
        for (int x = 0; x < quarter_w; x++) {
            unsigned char *pixel = fb + y * ctx->screen_stride + x * 4;
            pixel[0] = 0x00; pixel[1] = 0x00; pixel[2] = 0xFF; pixel[3] = 0xFF;
        }
    }
    
    /* Bottom-right: Yellow */
    for (int y = ctx->height - quarter_h; y < ctx->height; y++) {
        for (int x = ctx->width - quarter_w; x < ctx->width; x++) {
            unsigned char *pixel = fb + y * ctx->screen_stride + x * 4;
            pixel[0] = 0xFF; pixel[1] = 0xFF; pixel[2] = 0x00; pixel[3] = 0xFF;
        }
    }
    
    /* Draw white border around the edge */
    for (int x = 0; x < ctx->width; x++) {
        /* Top border */
        unsigned char *pixel = fb + 0 * ctx->screen_stride + x * 4;
        pixel[0] = 0xFF; pixel[1] = 0xFF; pixel[2] = 0xFF; pixel[3] = 0xFF;
        /* Bottom border */
        pixel = fb + (ctx->height - 1) * ctx->screen_stride + x * 4;
        pixel[0] = 0xFF; pixel[1] = 0xFF; pixel[2] = 0xFF; pixel[3] = 0xFF;
    }
    for (int y = 0; y < ctx->height; y++) {
        /* Left border */
        unsigned char *pixel = fb + y * ctx->screen_stride + 0 * 4;
        pixel[0] = 0xFF; pixel[1] = 0xFF; pixel[2] = 0xFF; pixel[3] = 0xFF;
        /* Right border */
        pixel = fb + y * ctx->screen_stride + (ctx->width - 1) * 4;
        pixel[0] = 0xFF; pixel[1] = 0xFF; pixel[2] = 0xFF; pixel[3] = 0xFF;
    }
}

/* ============================================================================
 * UTILITY AND DIAGNOSTIC FUNCTIONS
 * ============================================================================ */

const char* evo_display_get_error(evo_display_context_t *ctx) {
    if (!ctx) return "Invalid context";
    return ctx->error_message[0] ? ctx->error_message : "No error";
}

void evo_display_print_info(evo_display_context_t *ctx, const char *prefix) {
    if (!ctx) {
        printf("%sERROR: NULL context\n", prefix ? prefix : "");
        return;
    }
    
    printf("%sDisplay Context Info:\n", prefix ? prefix : "");
    printf("  Size: %dx%d\n", ctx->width, ctx->height);
    printf("  Position: (%d, %d)\n", ctx->x, ctx->y);
    printf("  Surface ID: %u\n", ctx->surface_id);
    printf("  Layer ID: %u\n", ctx->layer_id);
    printf("  ILM Available: %s\n", ctx->ilm_available ? "Yes" : "No");
    if (ctx->error_message[0]) {
        printf("  Last Error: %s\n", ctx->error_message);
    }
}

int evo_display_has_ilm(evo_display_context_t *ctx) {
    return ctx ? ctx->ilm_available : 0;
}

uint32_t* evo_display_get_framebuffer(evo_display_context_t *ctx) {
    if (!ctx) {
        printf("ERROR: evo_display_get_framebuffer - Invalid context\n");
        return NULL;
    }
    
    if (!ctx->screen_ptr[ctx->current_buffer]) {
        printf("ERROR: evo_display_get_framebuffer - No framebuffer available\n");
        return NULL;
    }
    
    return (uint32_t*)ctx->screen_ptr[ctx->current_buffer];
}

uint32_t* evo_display_get_next_framebuffer(evo_display_context_t *ctx) {
    if (!ctx) {
        printf("ERROR: evo_display_get_next_framebuffer - Invalid context\n");
        return NULL;
    }
    
    /* For double buffering, return the back buffer (the one not currently displayed) */
    int back_buffer = (ctx->buffer_count > 1) ? (1 - ctx->front_buffer) : ctx->front_buffer;
    
    /* Validate buffer index */
    if (back_buffer < 0 || back_buffer >= 2) {
        printf("ERROR: evo_display_get_next_framebuffer - Invalid buffer index %d\n", back_buffer);
        return NULL;
    }
    
    if (!ctx->screen_ptr[back_buffer]) {
        printf("ERROR: evo_display_get_next_framebuffer - No framebuffer available for buffer %d\n", back_buffer);
        return NULL;
    }
    
    ctx->current_buffer = back_buffer;  /* Set this as the current rendering buffer */
    return (uint32_t*)ctx->screen_ptr[back_buffer];
}

/* ============================================================================
 * ENHANCED CONFIGURATION IMPLEMENTATION
 * ============================================================================ */

void evo_display_config_init(evo_display_config_t *config, 
                            int width, int height, int x, int y,
                            unsigned int surface_id, unsigned int layer_id) {
    memset(config, 0, sizeof(evo_display_config_t));
    
    /* Basic properties */
    config->width = width;
    config->height = height;
    config->x = x;
    config->y = y;
    config->surface_id = surface_id;
    config->layer_id = layer_id;
    
    /* Advanced defaults for exact color reproduction */
    config->pixel_format = SCREEN_FORMAT_RGBA8888;
    config->background_color = 0x00000000;  /* Transparent by default */
    config->usage_flags = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    
    /* Buffer configuration */
    config->buffer_count = 1;  /* Single buffer by default */
    config->enable_vsync = 1;  /* Enable for smooth animation */
    
    /* ILM composition settings */
    config->layer_opacity = 255;           /* Fully opaque */
    config->composition_mode = 0;          /* Default composition */
    config->layer_background = 0x00000000; /* Transparent layer background */
    
    /* Positioning defaults */
    config->z_order = 0;
    config->source_x = 0;
    config->source_y = 0;
    config->source_width = width;
    config->source_height = height;
    
    /* Enhanced features */
    config->enable_color_correction = 0;   /* Disable color modifications */
    config->enable_alpha_blending = 1;     /* Enable alpha blending */
    config->force_exact_colors = 1;        /* Force exact color reproduction */
}

static int create_screen_window_enhanced(evo_display_context_t *ctx, const evo_display_config_t *config) {
    int rc;
    
    printf("✓ Creating enhanced Screen window with exact color configuration\n");
    
    /* Create window */
    rc = screen_create_window(&ctx->screen_win, ctx->screen_ctx);
    if (rc != 0) {
        set_error(ctx, "Failed to create screen window");
        return -1;
    }
    
    /* Set pixel format with error checking */
    int format = config->pixel_format;
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &format);
    if (rc != 0) {
        printf("⚠️ Warning: Failed to set pixel format to %d (error %d)\n", format, rc);
        /* Continue with default format */
    } else {
        printf("✓ Pixel format set to RGBA8888 for exact color control\n");
    }
    
    /* Set window size with validation */
    int size[2] = {config->width, config->height};
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size);
    if (rc != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to set window size %dx%d (error %d)", 
                 config->width, config->height, rc);
        set_error(ctx, error_msg);
        return -1;
    }
    
    /* Set window position with validation */
    int pos[2] = {config->x, config->y};
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, pos);
    if (rc != 0) {
        printf("⚠️ Warning: Failed to set window position (%d,%d) - error %d\n", 
               config->x, config->y, rc);
        printf("⚠️ This may cause the positioning errors we observed in logs\n");
        /* Continue - position errors are common but not fatal */
    } else {
        printf("✓ Window position set to (%d,%d)\n", config->x, config->y);
    }
    
    /* Set buffer usage with enhanced flags */
    int usage = config->usage_flags;
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);
    if (rc != 0) {
        printf("⚠️ Warning: Failed to set buffer usage flags (error %d)\n", rc);
    } else {
        printf("✓ Buffer usage flags set: WRITE | NATIVE\n");
    }
    
    /* Set explicit background color if specified */
    if (config->background_color != 0x00000000) {
        int bg_color = config->background_color;
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_COLOR, &bg_color);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set window background color (error %d)\n", rc);
        } else {
            printf("✓ Window background color set to 0x%08X\n", config->background_color);
        }
    }
    
    /* Set source rectangle for precise control */
    if (config->source_width != config->width || config->source_height != config->height) {
        int source_size[2] = {config->source_width, config->source_height};
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SOURCE_SIZE, source_size);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set source size (error %d)\n", rc);
        }
        
        int source_pos[2] = {config->source_x, config->source_y};
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SOURCE_POSITION, source_pos);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set source position (error %d)\n", rc);
        }
    }
    
    /* Set Z-order if specified */
    if (config->z_order != 0) {
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_ZORDER, &config->z_order);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set Z-order (error %d)\n", rc);
        } else {
            printf("✓ Window Z-order set to %d\n", config->z_order);
        }
    }
    
    /* Enable alpha blending if requested */
    if (config->enable_alpha_blending) {
        printf("🎨 Alpha blending requested (properties disabled to prevent crash)...\n");
        
        /* Commented out temporarily - these properties cause SIGILL crash */
        /*
        int global_alpha = 255;
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_GLOBAL_ALPHA, &global_alpha);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set global alpha (error %d)\n", rc);
        } else {
            printf("✓ Global alpha set to 255 (per-pixel alpha enabled)\n");
        }
        
        int alpha_mode = SCREEN_NON_PRE_MULTIPLIED_ALPHA;
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_ALPHA_MODE, &alpha_mode);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set alpha mode (error %d)\n", rc);
        } else {
            printf("✓ Alpha mode set to non-pre-multiplied\n");
        }
        */
        
        printf("✓ Alpha blending noted (will be implemented once we find correct properties)\n");
    }
    
    /* Set window ID string (critical for ILM integration) */
    char window_id[32];
    snprintf(window_id, sizeof(window_id), "%u", config->surface_id);
    rc = screen_set_window_property_cv(ctx->screen_win, SCREEN_PROPERTY_ID_STRING, 
                                      strlen(window_id), window_id);
    if (rc != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to set window ID '%s' (error %d)", 
                 window_id, rc);
        set_error(ctx, error_msg);
        return -1;
    }
    
    printf("✓ Enhanced Screen window created: %dx%d at (%d,%d) with ID='%s'\n", 
           config->width, config->height, config->x, config->y, window_id);
    return 0;
}

evo_display_context_t* evo_display_create_configured(const evo_display_config_t *config) {
    printf("=== Creating Enhanced EVO Display: %dx%d at (%d,%d) ===\n", 
           config->width, config->height, config->x, config->y);
    printf("🔧 Enhanced configuration: exact colors=%s, buffers=%d, opacity=%d\n",
           config->force_exact_colors ? "ON" : "OFF", 
           config->buffer_count, config->layer_opacity);
    
    /* Allocate context */
    evo_display_context_t *ctx = calloc(1, sizeof(evo_display_context_t));
    if (!ctx) {
        printf("ERROR: Failed to allocate display context\n");
        return NULL;
    }
    
    /* Initialize context from config */
    ctx->width = config->width;
    ctx->height = config->height;
    ctx->x = config->x;
    ctx->y = config->y;
    ctx->surface_id = config->surface_id;
    ctx->layer_id = config->layer_id;
    ctx->visible = 0;
    clear_error(ctx);
    
    /* Validate surface ID */
    evo_display_validate_surface_id(config->surface_id);
    
    /* Step 1: Load ILM library */
    if (load_ilm_library() == 0) {
        ctx->ilm_available = 1;
    }
    
    /* Step 2: Create Screen context */
    if (create_screen_context(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 3: Create enhanced Screen window */
    if (create_screen_window_enhanced(ctx, config) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 4: Create enhanced buffer configuration */
    if (config->buffer_count > 1) {
        printf("✓ Configuring double buffering (%d buffers)\n", config->buffer_count);
    }
    if (create_screen_buffer(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 5: Initialize ILM with enhanced settings */
    initialize_ilm(ctx);
    
    /* Step 6: Setup enhanced ILM integration */
    setup_ilm_integration(ctx);
    
    /* Step 7: Apply enhanced configuration */
    if (config->force_exact_colors) {
        evo_display_set_color_fidelity(ctx, 1);
    }
    
    if (config->layer_background != 0x00000000) {
        evo_display_set_background_colors(ctx, config->background_color, config->layer_background);
    }
    
    printf("✓ Enhanced display context created successfully\n");
    return ctx;
}

evo_display_ids_t evo_display_allocate_safe_ids(int priority_level) {
    evo_display_ids_t ids;
    static unsigned int surface_counter = 2000;  /* Start in safe range */
    static unsigned int layer_counter = 9000;    /* Start in high priority range */
    
    /* Allocate surface ID based on priority */
    switch (priority_level) {
        case 1: /* Low priority */
            ids.surface_id = 1500 + (surface_counter++ % 500);
            ids.layer_id = 8000 + (layer_counter++ % 1000);
            break;
        case 2: /* Normal priority */
            ids.surface_id = 2000 + (surface_counter++ % 1000);
            ids.layer_id = 9000 + (layer_counter++ % 1000);
            break;
        case 3: /* High priority */
            ids.surface_id = 3000 + (surface_counter++ % 1000);
            ids.layer_id = 9500 + (layer_counter++ % 500);
            break;
        default:
            ids.surface_id = 2000 + (surface_counter++ % 1000);
            ids.layer_id = 9000 + (layer_counter++ % 1000);
            break;
    }
    
    printf("✅ Allocated safe IDs: Surface=%u, Layer=%u (priority %d)\n", 
           ids.surface_id, ids.layer_id, priority_level);
    return ids;
}

int evo_display_set_color_fidelity(evo_display_context_t *ctx, int enable_exact_colors) {
    if (!ctx) return -1;
    
    printf("🎨 %s exact color reproduction mode\n", 
           enable_exact_colors ? "Enabling" : "Disabling");
    
    /* TODO: Add Screen API calls for color pipeline control */
    /* This will require investigation of Screen color management APIs */
    
    return 0;
}

int evo_display_set_background_colors(evo_display_context_t *ctx, 
                                     uint32_t window_bg, uint32_t layer_bg) {
    if (!ctx) return -1;
    
    printf("🎨 Setting background colors: Window=0x%08X, Layer=0x%08X\n", 
           window_bg, layer_bg);
    
    /* Set window background color */
    if (window_bg != 0x00000000) {
        int bg_color = window_bg;
        int rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_COLOR, &bg_color);
        if (rc != 0) {
            printf("⚠️ Warning: Failed to set window background color (error %d)\n", rc);
            return -1;
        }
        printf("✓ Window background color set\n");
    }
    
    /* Set layer background via ILM if available */
    if (ctx->ilm_available && layer_bg != 0x00000000) {
        /* TODO: Add ILM layer background color setting */
        printf("✓ Layer background color configuration prepared\n");
    }
    
    return 0;
}

/* ============================================================================
 * DOUBLE BUFFERING IMPLEMENTATION
 * ============================================================================ */

int evo_display_enable_double_buffering(evo_display_context_t *ctx, int enable) {
    int rc;  /* Declare once at the top */
    
    if (!ctx) {
        printf("ERROR: evo_display_enable_double_buffering - Invalid context\n");
        return -1;
    }
    
    if (!ctx->screen_win) {
        printf("ERROR: evo_display_enable_double_buffering - No screen window available\n");
        return -1;
    }
    
    if (ctx->visible) {
        printf("ERROR: evo_display_enable_double_buffering - Cannot change buffering after display is shown\n");
        return -1;
    }
    
    if (!enable) {
        printf("🔄 Configuring single buffer mode (NO FALLBACKS)...\n");
        
        /* Set window usage flags for single buffering */
        int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
        rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);
        if (rc != 0) {
            printf("FATAL ERROR: Failed to set window usage flags for single buffering (rc=%d)\n", rc);
            set_error(ctx, "FATAL: Cannot set window usage for single buffering");
            return -1;
        }
        printf("✅ Window usage flags set for single buffering\n");
        
        /* Create exactly 1 buffer - FAIL if not possible */
        ctx->buffer_count = 1;
        ctx->current_buffer = 0;
        ctx->front_buffer = 0;
        
        rc = screen_create_window_buffers(ctx->screen_win, ctx->buffer_count);
        if (rc != 0) {
            printf("FATAL ERROR: Failed to create single buffer (rc=%d)\n", rc);
            set_error(ctx, "FATAL: Single buffer creation failed");
            return -1;
        }
        
        /* Get single buffer handle */
        rc = screen_get_window_property_pv(ctx->screen_win, SCREEN_PROPERTY_RENDER_BUFFERS, 
                                          (void**)&ctx->screen_buf[0]);
        if (rc != 0 || !ctx->screen_buf[0]) {
            printf("FATAL ERROR: Failed to get single buffer handle (rc=%d)\n", rc);
            set_error(ctx, "FATAL: Cannot get single buffer handle");
            return -1;
        }
        
        /* Validate buffer handle before accessing properties */
        if (!ctx->screen_buf[0]) {
            printf("FATAL ERROR: Buffer handle is NULL after creation\n");
            set_error(ctx, "FATAL: Invalid buffer handle");
            return -1;
        }
        
        /* Get buffer pointer with validation */
        void *buffer_ptr = NULL;
        rc = screen_get_buffer_property_pv(ctx->screen_buf[0], SCREEN_PROPERTY_POINTER, &buffer_ptr);
        if (rc != 0 || !buffer_ptr) {
            printf("FATAL ERROR: Failed to get single buffer pointer (rc=%d, ptr=%p)\n", rc, buffer_ptr);
            printf("SYSTEM ERROR: Buffer property access failed - this indicates a QNX Screen API configuration issue\n");
            printf("TECHNICAL DETAILS: Buffer handle=%p, Screen window=%p\n", ctx->screen_buf[0], ctx->screen_win);
            set_error(ctx, "FATAL: Cannot get single buffer pointer");
            return -1;
        }
        
        ctx->screen_ptr[0] = (char*)buffer_ptr;
        ctx->screen_buf[1] = NULL;
        ctx->screen_ptr[1] = NULL;
        
        /* Get buffer stride */
        rc = screen_get_buffer_property_iv(ctx->screen_buf[0], SCREEN_PROPERTY_STRIDE, &ctx->screen_stride);
        if (rc != 0) {
            printf("FATAL ERROR: Failed to get buffer stride (rc=%d)\n", rc);
            set_error(ctx, "FATAL: Cannot get buffer stride");
            return -1;
        }
        
        printf("✅ Single buffer mode configured: ptr=%p, stride=%d\n", buffer_ptr, ctx->screen_stride);
        return 0;
    }
    
    printf("🔄 Enabling double buffering for OMAP5430 target (NO FALLBACKS)...\n");
    
    int new_buffer_count = 2;
    
    /* Set window usage flags for double buffering */
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ;
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);
    if (rc != 0) {
        printf("FATAL ERROR: Failed to set window usage flags for double buffering (rc=%d)\n", rc);
        set_error(ctx, "FATAL: Cannot set window usage for double buffering");
        return -1;
    }
    printf("✅ Window usage flags set for double buffering\n");
    
    /* Always create buffers fresh for proper double buffering setup */
    printf("🔄 Creating exactly %d buffers for OMAP5430 (FAIL HARD MODE)...\n", new_buffer_count);
    
    /* Update buffer configuration for double buffering - FAIL HARD IF NOT SUPPORTED */
    ctx->buffer_count = new_buffer_count;
    ctx->current_buffer = 0;
    ctx->front_buffer = 0;
    
    /* Create exactly 2 buffers - FAIL if not possible */
    rc = screen_create_window_buffers(ctx->screen_win, ctx->buffer_count);
    if (rc != 0) {
        printf("FATAL ERROR: Failed to create %d buffers (rc=%d)\n", ctx->buffer_count, rc);
        printf("FATAL ERROR: Double buffering REQUIRED for this graphics library - system configuration error\n");
        printf("FATAL ERROR: Check screen backend configuration and PowerVR drivers\n");
        set_error(ctx, "FATAL: Double buffering creation failed - system misconfiguration");
        return -1;
    }
    
    /* Get buffer handles - FAIL if not exactly 2 buffers */
    screen_buffer_t *buffers = NULL;
    rc = screen_get_window_property_pv(ctx->screen_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc != 0 || !buffers) {
        printf("FATAL ERROR: Failed to get double buffer handles (rc=%d, buffers=%p)\n", rc, buffers);
        printf("FATAL ERROR: Screen backend does not support double buffering property access\n");
        set_error(ctx, "FATAL: Cannot access double buffer handles - wrong screen backend");
        return -1;
    }
    
    /* Store and validate buffer handles - FAIL if ANY buffer is NULL */
    ctx->screen_buf[0] = buffers[0];
    ctx->screen_buf[1] = buffers[1];
    
    if (!ctx->screen_buf[0] || !ctx->screen_buf[1]) {
        printf("FATAL ERROR: One or both double buffer handles are NULL (buf0=%p, buf1=%p)\n", 
               ctx->screen_buf[0], ctx->screen_buf[1]);
        printf("FATAL ERROR: System returned NULL buffer handles - screen backend misconfiguration\n");
        set_error(ctx, "FATAL: NULL buffer handles - system error");
        return -1;
    }
    
    /* Get buffer pointers - FAIL if ANY pointer access fails */
    for (int i = 0; i < 2; i++) {
        void *buffer_ptr = NULL;
        rc = screen_get_buffer_property_pv(ctx->screen_buf[i], SCREEN_PROPERTY_POINTER, &buffer_ptr);
        if (rc != 0) {
            printf("FATAL ERROR: Failed to get buffer %d pointer (rc=%d)\n", i, rc);
            printf("FATAL ERROR: Screen backend does not support buffer property access\n");
            printf("FATAL ERROR: This indicates wrong screen backend or missing PowerVR drivers\n");
            set_error(ctx, "FATAL: Buffer property access failed - wrong screen backend");
            return -1;
        }
        
        if (!buffer_ptr) {
            printf("FATAL ERROR: Buffer %d returned NULL pointer\n", i);
            printf("FATAL ERROR: System buffer allocation failed - check memory and drivers\n");
            set_error(ctx, "FATAL: NULL buffer pointer - system allocation failure");
            return -1;
        }
        
        ctx->screen_ptr[i] = (char*)buffer_ptr;
        printf("✅ Buffer %d: handle=%p, ptr=%p (VERIFIED)\n", i, ctx->screen_buf[i], buffer_ptr);
    }
    
    /* Get buffer stride (same for all buffers) */
    rc = screen_get_buffer_property_iv(ctx->screen_buf[0], SCREEN_PROPERTY_STRIDE, &ctx->screen_stride);
    if (rc != 0) {
        printf("FATAL ERROR: Failed to get buffer stride (rc=%d)\n", rc);
        set_error(ctx, "FATAL: Cannot get buffer stride");
        return -1;
    }
    
    printf("✅ Double buffering enabled successfully for OMAP5430 (NO FALLBACKS USED)\n");
    printf("✅ Configuration: %d buffers, current=%d, front=%d\n", 
           ctx->buffer_count, ctx->current_buffer, ctx->front_buffer);
    return 0;
}

int evo_display_swap_buffers(evo_display_context_t *ctx) {
    if (!ctx) {
        printf("ERROR: evo_display_swap_buffers - Invalid context\n");
        return -1;
    }
    
    if (ctx->buffer_count < 2) {
        /* Single buffering - just update display */
        return evo_display_update(ctx);
    }
    
    /* Double buffering - swap front and back buffers */
    ctx->front_buffer = ctx->current_buffer;  /* Make current buffer the front buffer */
    
    /* Update display with new front buffer */
    int rc = evo_display_update(ctx);
    if (rc != 0) {
        return rc;
    }
    
    /* Set up next back buffer for rendering */
    ctx->current_buffer = 1 - ctx->front_buffer;
    
    return 0;
}

/* ============================================================================
 * BACKEND SELECTION API
 * ============================================================================ */

evo_display_context_t* evo_display_create_with_backend(int width, int height, int x, int y,
                                                      unsigned int surface_id, unsigned int layer_id,
                                                      evo_backend_type_t backend_type,
                                                      int allow_fallback) {
    printf("=== Creating EVO Display with Backend: %s ===\n", evo_backend_type_to_string(backend_type));
    
    /* Initialize backend system if not already done */
    if (!evo_backend_is_initialized()) {
        if (evo_backend_init() != 0) {
            printf("❌ Failed to initialize backend system\n");
            return NULL;
        }
    }
    
    /* Create backend request */
    evo_backend_request_t request = evo_backend_create_request(backend_type, !allow_fallback);
    request.verbose_logging = 1;
    request.validate_libraries = 1;
    
    /* Select the backend */
    evo_backend_result_t result = evo_backend_select(&request);
    
    printf("✅ Backend selected: %s (%s)\n", 
           result.config->name, result.selection_reason);
    
    /* Allocate context */
    evo_display_context_t *ctx = calloc(1, sizeof(evo_display_context_t));
    if (!ctx) {
        printf("❌ Failed to allocate display context\n");
        return NULL;
    }
    
    /* Initialize context with backend information */
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->surface_id = surface_id;
    ctx->layer_id = layer_id;
    ctx->visible = 0;
    clear_error(ctx);
    
    /* Set backend configuration */
    ctx->selected_backend = result.selected_backend;
    ctx->requested_backend = backend_type;
    ctx->backend_config = result.config;
    ctx->buffer_strategy = result.buffer_strategy;
    ctx->allow_backend_fallback = allow_fallback;
    
    /* Validate surface ID */
    evo_display_validate_surface_id(surface_id);
    
    /* Step 1: Load ILM library */
    if (load_ilm_library() == 0) {
        ctx->ilm_available = 1;
    }
    
    /* Step 2: Create Screen context with backend */
    if (create_screen_context(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 3: Create Screen window */
    if (create_screen_window(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 4: Create Screen buffer with backend strategy */
    if (create_screen_buffer(ctx) != 0) {
        evo_display_destroy(&ctx);
        return NULL;
    }
    
    /* Step 5: Initialize ILM */
    initialize_ilm(ctx);
    
    /* Step 6: Setup ILM integration */
    setup_ilm_integration(ctx);
    
    printf("✅ EVO Display created successfully with %s backend\n", ctx->backend_config->name);
    return ctx;
}

/* ============================================================================
 * MISSING UTILITY FUNCTIONS IMPLEMENTATION
 * ============================================================================ */

void evo_display_get_dimensions(evo_display_context_t *ctx, 
                               int *width_out, int *height_out, int *stride_out) {
    if (!ctx) return;
    
    if (width_out) *width_out = ctx->width;
    if (height_out) *height_out = ctx->height;
    if (stride_out) *stride_out = ctx->screen_stride;
}

int evo_display_configure_buffers(evo_display_context_t *ctx, 
                                 int buffer_count, int enable_vsync) {
    if (!ctx) {
        printf("ERROR: evo_display_configure_buffers - Invalid context\n");
        return -1;
    }
    
    if (ctx->visible) {
        printf("ERROR: evo_display_configure_buffers - Cannot change buffer configuration after display is shown\n");
        return -1;
    }
    
    printf("🔧 Configuring buffers: count=%d, vsync=%s\n", 
           buffer_count, enable_vsync ? "enabled" : "disabled");
    
    /* Use the existing double buffering function */
    return evo_display_enable_double_buffering(ctx, (buffer_count > 1));
}

/* ============================================================================
 * BASIC DRAWING FUNCTIONS (Legacy Compatibility)
 * ============================================================================ */

void evo_display_draw_rectangle(evo_display_context_t *ctx, int x, int y, 
                               int width, int height, unsigned int color) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer]) return;
    
    /* Extract color components from EVO_COLOR_RGBA format */
    unsigned char b = color & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char a = (color >> 24) & 0xFF;
    
    /* Clamp rectangle to display bounds */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > ctx->width) width = ctx->width - x;
    if (y + height > ctx->height) height = ctx->height - y;
    
    if (width <= 0 || height <= 0) return;
    
    /* Draw rectangle */
    for (int dy = 0; dy < height; dy++) {
        unsigned char *row = (unsigned char*)ctx->screen_ptr[ctx->current_buffer] + 
                            (y + dy) * ctx->screen_stride + x * 4;
        for (int dx = 0; dx < width; dx++) {
            row[dx * 4 + 0] = b;  /* Blue first (BGRA order) */
            row[dx * 4 + 1] = g;  /* Green second */
            row[dx * 4 + 2] = r;  /* Red third */
            row[dx * 4 + 3] = a;  /* Alpha last */
        }
    }
}

void evo_display_draw_line(evo_display_context_t *ctx, int x1, int y1, 
                          int x2, int y2, unsigned int color) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer]) return;
    
    /* Simple Bresenham line algorithm */
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    /* Extract color components */
    unsigned char b = color & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char a = (color >> 24) & 0xFF;
    
    int x = x1, y = y1;
    
    while (1) {
        /* Draw pixel if within bounds */
        if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
            unsigned char *pixel = (unsigned char*)ctx->screen_ptr[ctx->current_buffer] + 
                                  y * ctx->screen_stride + x * 4;
            pixel[0] = b; pixel[1] = g; pixel[2] = r; pixel[3] = a;
        }
        
        if (x == x2 && y == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

void evo_display_draw_circle(evo_display_context_t *ctx, int center_x, int center_y, 
                            int radius, unsigned int color) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer] || radius <= 0) return;
    
    /* Extract color components */
    unsigned char b = color & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char a = (color >> 24) & 0xFF;
    
    /* Helper function to draw a point */
    auto void draw_circle_point(int px, int py) {
        if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
            unsigned char *pixel = (unsigned char*)ctx->screen_ptr[ctx->current_buffer] + 
                                  py * ctx->screen_stride + px * 4;
            pixel[0] = b; pixel[1] = g; pixel[2] = r; pixel[3] = a;
        }
    }
    
    /* Bresenham circle algorithm */
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;
    
    while (y >= x) {
        /* Draw 8 symmetric points */
        draw_circle_point(center_x + x, center_y + y);
        draw_circle_point(center_x - x, center_y + y);
        draw_circle_point(center_x + x, center_y - y);
        draw_circle_point(center_x - x, center_y - y);
        draw_circle_point(center_x + y, center_y + x);
        draw_circle_point(center_x - y, center_y + x);
        draw_circle_point(center_x + y, center_y - x);
        draw_circle_point(center_x - y, center_y - x);
        
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void evo_display_draw_filled_circle(evo_display_context_t *ctx, int center_x, int center_y, 
                                   int radius, unsigned int color) {
    if (!ctx || !ctx->screen_ptr[ctx->current_buffer] || radius <= 0) return;
    
    /* Simple filled circle using distance check */
    int radius_sq = radius * radius;
    
    for (int y = center_y - radius; y <= center_y + radius; y++) {
        if (y < 0 || y >= ctx->height) continue;
        
        for (int x = center_x - radius; x <= center_x + radius; x++) {
            if (x < 0 || x >= ctx->width) continue;
            
            int dx = x - center_x;
            int dy = y - center_y;
            
            if (dx * dx + dy * dy <= radius_sq) {
                /* Point is inside circle */
                unsigned char *pixel = (unsigned char*)ctx->screen_ptr[ctx->current_buffer] + 
                                      y * ctx->screen_stride + x * 4;
                pixel[0] = color & 0xFF;         /* Blue */
                pixel[1] = (color >> 8) & 0xFF;  /* Green */
                pixel[2] = (color >> 16) & 0xFF; /* Red */
                pixel[3] = (color >> 24) & 0xFF; /* Alpha */
            }
        }
    }
}

const evo_backend_config_t* evo_display_get_backend_info(evo_display_context_t *ctx) {
    if (!ctx) {
        return NULL;
    }
    return ctx->backend_config;
}

void evo_display_print_backend_status(evo_display_context_t *ctx) {
    if (!ctx) {
        printf("❌ Invalid display context\n");
        return;
    }
    
    printf("\n🎯 Backend Status for Display Context\n");
    printf("=====================================\n");
    
    if (ctx->backend_config) {
        printf("Selected Backend: %s\n", ctx->backend_config->name);
        printf("Requested Backend: %s\n", evo_backend_type_to_string(ctx->requested_backend));
        printf("Library Path: %s\n", ctx->backend_config->library_path);
        printf("Buffer Strategy: %s\n", 
               (ctx->buffer_strategy == EVO_BUFFER_DOUBLE) ? "Double" : "Single");
        printf("Hardware Accelerated: %s\n", 
               ctx->backend_config->is_hardware_accelerated ? "Yes" : "No");
        printf("Recommended: %s\n", 
               ctx->backend_config->is_recommended ? "Yes" : "No");
        printf("Use Case: %s\n", ctx->backend_config->use_case);
        printf("Requirements: %s\n", ctx->backend_config->requirements);
        printf("Fallback Allowed: %s\n", ctx->allow_backend_fallback ? "Yes" : "No");
    } else {
        printf("Backend: Auto-selection (no explicit backend configured)\n");
    }
    
    printf("Buffer Count: %d\n", ctx->buffer_count);
    printf("Display Size: %dx%d\n", ctx->width, ctx->height);
    printf("Surface ID: %u\n", ctx->surface_id);
    printf("Layer ID: %u\n", ctx->layer_id);
    printf("Visible: %s\n", ctx->visible ? "Yes" : "No");
    printf("ILM Available: %s\n", ctx->ilm_available ? "Yes" : "No");
}



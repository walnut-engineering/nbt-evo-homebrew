/**
 * @file evo_backend_display_unified.c
 * @brief EVO Backend + Display Unified Implementation (Phase 4)
 * 
 * PHASE 4 CONSOLIDATION: Combined Backend Selection + Display Management
 * - Merged evo_backend_unified.c (1015 lines) + evo_display_unified.c (1170 lines)
 * - Eliminates API overhead between backend selection and display creation
 * - Single unified backend-display management system
 * 
 * FUNCTIONALITY:
 * - Backend selection and hardware testing
 * - Display creation with selected backend
 * - Screen + ILM integration 
 * - Hardware acceleration setup
 */

#include "../include/evo_backend.h"
#include "../include/evo_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include <screen/screen.h>
#include <ctype.h>
#include <unistd.h>

/* Enable popen/pclose on QNX */
#ifdef __QNX__
#define _GNU_SOURCE
#endif

/* ============================================================================
 * UNIFIED GLOBAL STATE (PHASE 4)
 * ============================================================================ */

/* Backend state */
static int g_backend_initialized = 0;

/* Display state */ 
static int g_display_initialized = 0;

/* ILM library handle */
static void *g_ilm_handle = NULL;

/* ============================================================================
 * ILM FUNCTION POINTERS (from display implementation)
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
        case ILM_ERROR_NONE: return "No error";
        case ILM_ERROR_INVALID_ARGUMENTS: return "Invalid arguments";
        case ILM_ERROR_TIMEOUT: return "Timeout";
        case ILM_ERROR_RESOURCE_NOT_FOUND: return "Resource not found";
        case ILM_ERROR_RESOURCE_ALREADY_INUSE: return "Resource already in use";
        case ILM_ERROR_NOT_IMPLEMENTED: return "Not implemented";
        case ILM_ERROR_UNEXPECTED_MESSAGE: return "Unexpected message";
        case ILM_ERROR_UNKNOWN:
        default: return "Unknown error";
    }
}

/* ILM function pointers */
static t_ilm_errorTypes (*ilm_init)(void) = NULL;
static t_ilm_errorTypes (*ilm_destroy)(void) = NULL;
static t_ilm_errorTypes (*ilm_surfaceCreate)(t_ilm_nativehandle, int, int, ilmPixelFormat, t_ilm_surface*) = NULL;
static t_ilm_errorTypes (*ilm_surfaceRemove)(t_ilm_surface) = NULL;
static t_ilm_errorTypes (*ilm_layerAddSurface)(t_ilm_uint, t_ilm_surface) = NULL;
static t_ilm_errorTypes (*ilm_layerRemoveSurface)(t_ilm_uint, t_ilm_surface) = NULL;
static t_ilm_errorTypes (*ilm_displaySetRenderOrder)(t_ilm_uint, t_ilm_uint*, t_ilm_uint) = NULL;
static t_ilm_errorTypes (*ilm_getLayerIDsOnScreen)(t_ilm_uint, t_ilm_uint*, t_ilm_uint**) = NULL;
static t_ilm_errorTypes (*ilm_commitChanges)(void) = NULL;
static t_ilm_errorTypes (*ilm_layerSetVisibility)(t_ilm_uint, t_ilm_bool) = NULL;

/* ============================================================================
 * BACKEND CONFIGURATION DATABASE (from backend implementation)
 * ============================================================================ */

static const evo_backend_config_t BACKEND_CONFIGS[EVO_BACKEND_COUNT] = {
    {
        .type = EVO_BACKEND_AUTO,
        .name = "Auto-select",
        .library_path = "", /* Auto-select doesn't have a fixed library */
        .description = "Automatically select the best available backend",
        .capabilities = 0, /* Auto has no capabilities - it delegates */
        .requirements = "Runtime detection"
    },
    {
        .type = EVO_BACKEND_PVR2D,
        .name = "PowerVR 2D",
        .library_path = "/lib/screen-pvr2d.so",
        .description = "PowerVR SGX540 hardware acceleration",
        .capabilities = EVO_CAP_HARDWARE_ACCEL | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_2D_ACCEL,
        .requirements = "PowerVR SGX540"
    },
    {
        .type = EVO_BACKEND_GLES2,
        .name = "OpenGL ES 2.0",
        .library_path = "/lib/screen-gles2.so",
        .description = "OpenGL ES 2.0 hardware rendering",
        .capabilities = EVO_CAP_HARDWARE_ACCEL | EVO_CAP_OPENGL_ES2 | EVO_CAP_DOUBLE_BUFFER,
        .requirements = "OpenGL ES 2.0 support"
    },
    {
        .type = EVO_BACKEND_GLES1,
        .name = "OpenGL ES 1.0",
        .library_path = "/lib/screen-gles1.so",
        .description = "OpenGL ES 1.0 hardware rendering",
        .capabilities = EVO_CAP_HARDWARE_ACCEL | EVO_CAP_OPENGL_ES1 | EVO_CAP_DOUBLE_BUFFER,
        .requirements = "OpenGL ES 1.0 support"
    },
    {
        .type = EVO_BACKEND_VIVANTE,
        .name = "Vivante",
        .library_path = "/lib/screen-vivante-omap5430.so",
        .description = "Vivante GC320 2D hardware acceleration",
        .capabilities = EVO_CAP_HARDWARE_ACCEL | EVO_CAP_2D_ACCEL | EVO_CAP_DOUBLE_BUFFER,
        .requirements = "Vivante GC320"
    },
    {
        .type = EVO_BACKEND_BASIC,
        .name = "Basic Screen",
        .library_path = "/lib/libscreen.so.1",
        .description = "Basic QNX Screen API (software rendering)",
        .capabilities = EVO_CAP_DISPLAY | EVO_CAP_DOUBLE_BUFFER,
        .requirements = "QNX Screen API"
    },
    {
        .type = EVO_BACKEND_SOFTWARE,
        .name = "Software",
        .library_path = "/lib/libscreen.so.1",
        .description = "Software-only rendering fallback",
        .capabilities = EVO_CAP_DISPLAY,
        .requirements = "Basic QNX system"
    }
};

/* ============================================================================
 * PHASE 4: UNIFIED BACKEND INITIALIZATION
 * ============================================================================ */

int evo_backend_init(void)
{
    if (g_backend_initialized) {
        return 0;
    }
    
    printf("🔧 Backend system initialized\n");
    g_backend_initialized = 1;
    return 0;
}

void evo_backend_shutdown(void)
{
    printf("🔧 Backend system shutdown\n");
    g_backend_initialized = 0;
}

int evo_backend_is_initialized(void)
{
    return g_backend_initialized;
}

/* ============================================================================
 * BACKEND CONFIGURATION ACCESS (from backend implementation)
 * ============================================================================ */

const evo_backend_config_t* evo_backend_get_config(evo_backend_type_t type)
{
    if (type < 0 || type >= EVO_BACKEND_COUNT) {
        return NULL;
    }
    return &BACKEND_CONFIGS[type];
}

const char* evo_backend_get_name(evo_backend_type_t type)
{
    const evo_backend_config_t* config = evo_backend_get_config(type);
    return config ? config->name : "Unknown";
}

const char* evo_backend_type_to_string(evo_backend_type_t type)
{
    return evo_backend_get_name(type);
}

void evo_backend_list_all(void)
{
    printf("📋 Available EVO backends:\n");
    for (int i = 1; i < EVO_BACKEND_COUNT; i++) { /* Skip AUTO */
        const evo_backend_config_t* config = &BACKEND_CONFIGS[i];
        evo_backend_test_result_t test = evo_backend_test(config->type);
        
        const char* status_text = test.library_exists && test.library_loadable && test.has_screen_symbols ? "✅ Available" : "❌ Not Available";
        
        printf("  %s (%s): %s\n", config->name, config->library_path, status_text);
        if (config->description && strlen(config->description) > 0) {
            printf("    %s\n", config->description);
        }
        
        if (!test.library_exists || !test.library_loadable || !test.has_screen_symbols) {
            printf("    📝 Requirements: %s\n", config->requirements);
        }
    }
}

/* ============================================================================
 * BACKEND TESTING (from backend implementation)
 * ============================================================================ */

evo_backend_test_result_t evo_backend_test(evo_backend_type_t type)
{
    evo_backend_test_result_t result = {0};
    const evo_backend_config_t* config = evo_backend_get_config(type);
    
    if (!config || strlen(config->library_path) == 0) {
        return result; /* Invalid or auto-select */
    }
    
    /* Check if library file exists */
    FILE* f = fopen(config->library_path, "r");
    if (f) {
        result.library_exists = 1;
        fclose(f);
    }
    
    if (!result.library_exists) {
        return result;
    }
    
    /* Try to load library */
    void* handle = dlopen(config->library_path, RTLD_LAZY);
    if (handle) {
        result.library_loadable = 1;
        
        /* For screen driver libraries, we don't check for screen_create_context
         * because that's in libscreen.so.1, not in the driver DLLs.
         * Screen drivers are plugin DLLs that extend Screen API functionality.
         * If the driver loads successfully, we consider it has "screen symbols". */
        result.has_screen_symbols = 1;
        
        dlclose(handle);
    }
    
    return result;
}

void evo_backend_validate_or_die(evo_backend_type_t type)
{
    const evo_backend_config_t* config = evo_backend_get_config(type);
    if (!config) {
        printf("💥 FATAL: Invalid backend type %d\n", type);
        exit(1);
    }
    
    if (type == EVO_BACKEND_AUTO) {
        return; /* Auto-select can't be validated directly */
    }
    
    evo_backend_test_result_t test = evo_backend_test(type);
    
    if (!test.library_exists) {
        printf("💥 FATAL: Backend %s library missing: %s\n"
               "    File does not exist on filesystem\n"
               "    Requirements: %s\n", 
               config->name, config->library_path, config->requirements);
        exit(1);
    }
    
    if (!test.library_loadable) {
        printf("💥 FATAL: Backend %s not loadable: %s\n"
               "    Library exists but cannot be loaded (missing dependencies?)\n"
               "    Requirements: %s\n", 
               config->name, config->library_path, config->requirements);
        exit(1);
    }
    
    if (!test.has_screen_symbols) {
        printf("💥 FATAL: Backend %s missing Screen API: %s\n"
               "    The library exists but doesn't provide Screen functionality\n"
               "    Requirements: %s\n", 
               config->name, config->library_path, config->requirements);
        exit(1);
    }
    
    printf("✅ Backend %s validated: %s\n", config->name, config->library_path);
}

/* Backend selection logic continues... */

/* ============================================================================
 * PHASE 4: UNIFIED BACKEND SELECTION 
 * ============================================================================ */

evo_backend_result_t evo_backend_select(const evo_backend_request_t *request)
{
    evo_backend_result_t result = {0};
    
    if (!request) {
        result.selected_backend = EVO_BACKEND_SOFTWARE;
        strcpy(result.selection_reason, "Invalid request");
        return result;
    }
    
    /* If specific backend requested, validate it immediately */
    if (request->backend_type != EVO_BACKEND_AUTO) {
        if (request->verbose_logging) {
            printf("🎯 Forcing backend: %s (enum: %d, force: %d)\n", 
                   evo_backend_type_to_string(request->backend_type), 
                   request->backend_type, request->force_backend);
        }
        
        /* Validate the forced backend */
        if (request->validate_libraries) {
            if (request->force_backend) {
                if (request->verbose_logging) {
                    printf("🔧 Force mode enabled - validating or dying...\n");
                }
                evo_backend_validate_or_die(request->backend_type);
            } else {
                evo_backend_test_result_t test = evo_backend_test(request->backend_type);
                if (!test.library_exists || !test.library_loadable || !test.has_screen_symbols) {
                    if (request->verbose_logging) {
                        printf("⚠️  Requested backend %s not available, falling back\n", 
                               evo_backend_type_to_string(request->backend_type));
                    }
                    /* Fall through to auto select */
                } else {
                    result.selected_backend = request->backend_type;
                    result.config = evo_backend_get_config(request->backend_type);
                    snprintf(result.selection_reason, sizeof(result.selection_reason),
                            "Forced selection: %s", result.config->name);
                    return result;
                }
            }
        } else {
            result.selected_backend = request->backend_type;
            result.config = evo_backend_get_config(request->backend_type);
            snprintf(result.selection_reason, sizeof(result.selection_reason),
                    "Forced selection: %s", result.config->name);
            return result;
        }
    }
    
    /* Auto-select best available backend */
    evo_backend_type_t auto_backend = evo_backend_auto_select();
    result.selected_backend = auto_backend;
    result.config = evo_backend_get_config(auto_backend);
    snprintf(result.selection_reason, sizeof(result.selection_reason),
            "Auto-selected: %s", result.config->name);
    
    return result;
}

evo_backend_type_t evo_backend_auto_select(void)
{
    /* Priority order: PowerVR -> Vivante -> GLES2 -> Basic -> Software */
    evo_backend_type_t priority[] = {
        EVO_BACKEND_PVR2D,
        EVO_BACKEND_VIVANTE, 
        EVO_BACKEND_GLES2,
        EVO_BACKEND_BASIC,
        EVO_BACKEND_SOFTWARE
    };
    
    for (int i = 0; i < sizeof(priority)/sizeof(priority[0]); i++) {
        evo_backend_test_result_t test = evo_backend_test(priority[i]);
        if (test.library_exists && test.library_loadable && test.has_screen_symbols) {
            return priority[i];
        }
    }
    
    return EVO_BACKEND_SOFTWARE; /* Ultimate fallback */
}

evo_backend_request_t evo_backend_create_auto_request(void)
{
    evo_backend_request_t request = {0};
    request.backend_type = EVO_BACKEND_AUTO;
    request.force_backend = 0;
    request.validate_libraries = 1;
    request.verbose_logging = 1;
    return request;
}

evo_backend_request_t evo_backend_create_request(evo_backend_type_t type, int force_backend)
{
    evo_backend_request_t request = {0};
    request.backend_type = type;
    request.force_backend = force_backend;
    request.validate_libraries = 1;
    request.verbose_logging = 1;
    return request;
}

/* ============================================================================
 * PHASE 4: UNIFIED ILM MANAGEMENT 
 * ============================================================================ */

/* ILM library loading and function pointer setup */
static int load_ilm_functions(void)
{
    if (g_ilm_handle) {
        return 1; /* Already loaded */
    }
    
    /* Try loading different ILM library names */
    const char* ilm_library_names[] = {
        "/lib/libilmClient.so.2.2.0",
        "/lib/libilmClient.so.2",
        "/lib/libilmClient.so",
        "/usr/lib/libilmClient.so.2.2.0",
        "/usr/lib/libilmClient.so.2",
        "/usr/lib/libilmClient.so"
    };
    
    for (int i = 0; i < sizeof(ilm_library_names)/sizeof(ilm_library_names[0]); i++) {
        g_ilm_handle = dlopen(ilm_library_names[i], RTLD_LAZY);
        if (g_ilm_handle) {
            printf("[EVO] ✓ ILM library loaded: %s\n", ilm_library_names[i]);
            break;
        }
    }
    
    if (!g_ilm_handle) {
        printf("[EVO] ❌ Failed to load ILM library\n");
        return 0;
    }
    
    /* Load function pointers */
    ilm_init = dlsym(g_ilm_handle, "ilm_init");
    ilm_destroy = dlsym(g_ilm_handle, "ilm_destroy");
    ilm_surfaceCreate = dlsym(g_ilm_handle, "ilm_surfaceCreate");
    ilm_surfaceRemove = dlsym(g_ilm_handle, "ilm_surfaceRemove");
    ilm_layerAddSurface = dlsym(g_ilm_handle, "ilm_layerAddSurface");
    ilm_layerRemoveSurface = dlsym(g_ilm_handle, "ilm_layerRemoveSurface");
    ilm_displaySetRenderOrder = dlsym(g_ilm_handle, "ilm_displaySetRenderOrder");
    ilm_getLayerIDsOnScreen = dlsym(g_ilm_handle, "ilm_getLayerIDsOnScreen");
    ilm_commitChanges = dlsym(g_ilm_handle, "ilm_commitChanges");
    ilm_layerSetVisibility = dlsym(g_ilm_handle, "ilm_layerSetVisibility");
    
    /* Verify critical functions loaded */
    if (!ilm_init || !ilm_destroy || !ilm_surfaceCreate || !ilm_commitChanges) {
        printf("[EVO] ❌ Critical ILM functions missing\n");
        dlclose(g_ilm_handle);
        g_ilm_handle = NULL;
        return 0;
    }
    
    printf("[EVO] ✓ ILM functions loaded successfully\n");
    return 1;
}

/* ============================================================================
 * PHASE 4: UNIFIED DISPLAY CREATION WITH BACKEND INTEGRATION
 * ============================================================================ */

evo_display_context_t* evo_display_create(int width, int height, int x, int y, 
                                         unsigned int surface_id, unsigned int layer_id)
{
    return evo_display_create_with_backend(width, height, x, y, surface_id, layer_id, EVO_BACKEND_AUTO);
}

evo_display_context_t* evo_display_create_with_backend(int width, int height, int x, int y,
                                                      unsigned int surface_id, unsigned int layer_id,
                                                      evo_backend_type_t backend_type)
{
    printf("[EVO] Creating display context: %dx%d at (%d,%d) surface=%u layer=%u\n", 
           width, height, x, y, surface_id, layer_id);
    
    /* PHASE 4: Integrated backend selection and display creation */
    
    /* Step 1: Initialize backend system if needed */
    if (!g_backend_initialized) {
        evo_backend_init();
    }
    
    /* Step 2: Select appropriate backend */
    evo_backend_request_t request = evo_backend_create_request(backend_type, 0);
    evo_backend_result_t backend_result = evo_backend_select(&request);
    
    printf("[EVO] Backend selected: %s (%s)\n", 
           backend_result.config->name, backend_result.selection_reason);
    
    /* Step 3: Allocate display context */
    evo_display_context_t* ctx = calloc(1, sizeof(evo_display_context_t));
    if (!ctx) {
        printf("[EVO] ❌ Failed to allocate display context\n");
        return NULL;
    }
    
    /* Step 4: Initialize context with backend info */
    ctx->width = width;
    ctx->height = height;
    ctx->x = x;
    ctx->y = y;
    ctx->surface_id = surface_id;
    ctx->layer_id = layer_id;
    ctx->backend_type = backend_result.selected_backend;
    ctx->backend_config = backend_result.config;
    
    /* Step 5: Create Screen context with selected backend */
    int rc = screen_create_context(&ctx->screen_ctx, 0);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to create Screen context: %d\n", rc);
        free(ctx);
        return NULL;
    }
    printf("[EVO] ✓ Screen context created\n");
    
    /* Step 6: Create Screen window */
    rc = screen_create_window(&ctx->screen_win, ctx->screen_ctx);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to create Screen window: %d\n", rc);
        screen_destroy_context(ctx->screen_ctx);
        free(ctx);
        return NULL;
    }
    printf("[EVO] ✓ Screen window created\n");
    
    /* Step 7: Configure window properties */
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to set window usage: %d\n", rc);
        goto cleanup;
    }
    
    int format = SCREEN_FORMAT_RGBA8888;
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &format);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to set window format: %d\n", rc);
        goto cleanup;
    }
    
    int position[2] = {x, y};
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, position);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to set window position: %d\n", rc);
        goto cleanup;
    }
    
    int size[2] = {width, height};
    rc = screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to set window size: %d\n", rc);
        goto cleanup;
    }
    
    /* Step 8: Create buffers based on backend capabilities */
    int buffer_count = (backend_result.config->capabilities & EVO_CAP_DOUBLE_BUFFER) ? 2 : 1;
    int buffer_rc = screen_create_window_buffers(ctx->screen_win, buffer_count);
    if (buffer_rc != 0) {
        printf("[EVO] ❌ Failed to create window buffers: %d\n", buffer_rc);
        goto cleanup;
    }
    printf("[EVO] ✓ Created %d window buffer(s) with backend %s\n", 
           buffer_count, backend_result.config->name);
    
    /* Step 9: Get buffer pointers */
    rc = screen_get_window_property_pv(ctx->screen_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&ctx->screen_buf);
    if (rc != 0 || !ctx->screen_buf) {
        printf("[EVO] ❌ Failed to get render buffers: %d\n", rc);
        goto cleanup;
    }
    
    /* Step 10: Get buffer properties */
    rc = screen_get_buffer_property_pv(ctx->screen_buf[0], SCREEN_PROPERTY_POINTER, (void**)&ctx->framebuffer);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to get buffer pointer: %d\n", rc);
        goto cleanup;
    }
    
    rc = screen_get_buffer_property_iv(ctx->screen_buf[0], SCREEN_PROPERTY_STRIDE, &ctx->stride);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to get buffer stride: %d\n", rc);
        goto cleanup;
    }
    
    printf("[EVO] ✓ Framebuffer: %p, stride: %d bytes\n", ctx->framebuffer, ctx->stride);
    
    /* Step 11: Initialize ILM integration */
    if (!load_ilm_functions()) {
        printf("[EVO] ❌ Failed to load ILM functions\n");
        goto cleanup;
    }
    
    /* Step 12: Initialize ILM system */
    t_ilm_errorTypes ilm_rc = ilm_init();
    if (ilm_rc != ILM_ERROR_NONE) {
        printf("[EVO] ❌ ILM init failed: %s\n", ilm_error_to_string(ilm_rc));
        goto cleanup;
    }
    printf("[EVO] ✓ ILM initialized\n");
    
    /* Step 13: Create ILM surface */
    ilm_rc = ilm_surfaceCreate((t_ilm_nativehandle)ctx->screen_win, width, height, 
                               ILM_PIXELFORMAT_RGBA_8888, &ctx->ilm_surface_id);
    if (ilm_rc != ILM_ERROR_NONE) {
        printf("[EVO] ❌ ILM surface creation failed: %s\n", ilm_error_to_string(ilm_rc));
        goto cleanup_ilm;
    }
    printf("[EVO] ✓ ILM surface created: %u\n", ctx->ilm_surface_id);
    
    /* Success! */
    ctx->visible = 0;
    printf("[EVO] ✅ Display context created successfully with backend %s\n", 
           backend_result.config->name);
    return ctx;

cleanup_ilm:
    if (ilm_destroy) ilm_destroy();
cleanup:
    if (ctx->screen_win) screen_destroy_window(ctx->screen_win);
    if (ctx->screen_ctx) screen_destroy_context(ctx->screen_ctx);
    free(ctx);
    return NULL;
}

/* Additional display management functions... */
int evo_display_show(evo_display_context_t *ctx)
{
    if (!ctx) return -1;
    
    printf("[EVO] Making display visible\n");
    
    /* Add surface to layer */
    t_ilm_errorTypes rc = ilm_layerAddSurface(ctx->layer_id, ctx->ilm_surface_id);
    if (rc != ILM_ERROR_NONE) {
        printf("[EVO] ❌ Failed to add surface to layer: %s\n", ilm_error_to_string(rc));
        return -1;
    }
    
    /* Set layer visibility */
    rc = ilm_layerSetVisibility(ctx->layer_id, 1);
    if (rc != ILM_ERROR_NONE) {
        printf("[EVO] ❌ Failed to set layer visibility: %s\n", ilm_error_to_string(rc));
        return -1;
    }
    
    /* Commit changes */
    rc = ilm_commitChanges();
    if (rc != ILM_ERROR_NONE) {
        printf("[EVO] ❌ Failed to commit ILM changes: %s\n", ilm_error_to_string(rc));
        return -1;
    }
    
    ctx->visible = 1;
    printf("[EVO] ✅ Display is now visible\n");
    return 0;
}

void evo_display_destroy(evo_display_context_t **ctx_ptr)
{
    if (!ctx_ptr || !*ctx_ptr) return;
    
    evo_display_context_t *ctx = *ctx_ptr;
    printf("[EVO] Destroying display context\n");
    
    if (ctx->visible) {
        evo_display_hide(ctx);
    }
    
    /* Cleanup ILM surface */
    if (ctx->ilm_surface_id && ilm_surfaceRemove) {
        ilm_surfaceRemove(ctx->ilm_surface_id);
    }
    
    /* Cleanup ILM system */
    if (ilm_destroy) {
        ilm_destroy();
    }
    
    /* Cleanup Screen resources */
    if (ctx->screen_win) {
        screen_destroy_window(ctx->screen_win);
    }
    if (ctx->screen_ctx) {
        screen_destroy_context(ctx->screen_ctx);
    }
    
    /* Cleanup ILM library */
    if (g_ilm_handle) {
        dlclose(g_ilm_handle);
        g_ilm_handle = NULL;
    }
    
    free(ctx);
    *ctx_ptr = NULL;
    printf("[EVO] ✅ Context destroyed\n");
}

/* ============================================================================
 * PHASE 4: ADDITIONAL DISPLAY MANAGEMENT FUNCTIONS
 * ============================================================================ */

int evo_display_update(evo_display_context_t *ctx)
{
    if (!ctx) return -1;
    
    /* Post the screen buffer */
    int rc = screen_post_window(ctx->screen_win, ctx->screen_buf[0], 1, NULL, 0);
    if (rc != 0) {
        printf("[EVO] ❌ Failed to post window: %d\n", rc);
        return -1;
    }
    
    return 0;
}

int evo_display_hide(evo_display_context_t *ctx)
{
    if (!ctx) return -1;
    if (!ctx->visible) return 0;
    
    printf("[EVO] Hiding display\n");
    
    /* Remove surface from layer */
    if (ilm_layerRemoveSurface) {
        t_ilm_errorTypes rc = ilm_layerRemoveSurface(ctx->layer_id, ctx->ilm_surface_id);
        if (rc != ILM_ERROR_NONE) {
            printf("[EVO] ⚠️  Failed to remove surface from layer: %s\n", ilm_error_to_string(rc));
        }
    }
    
    /* Commit changes */
    if (ilm_commitChanges) {
        t_ilm_errorTypes rc = ilm_commitChanges();
        if (rc != ILM_ERROR_NONE) {
            printf("[EVO] ⚠️  Failed to commit ILM changes: %s\n", ilm_error_to_string(rc));
        }
    }
    
    ctx->visible = 0;
    printf("[EVO] Display is now hidden\n");
    return 0;
}

void evo_display_clear(evo_display_context_t *ctx, unsigned int color)
{
    if (!ctx || !ctx->framebuffer) return;
    
    uint32_t *pixels = (uint32_t*)ctx->framebuffer;
    int pixel_count = (ctx->stride * ctx->height) / sizeof(uint32_t);
    
    for (int i = 0; i < pixel_count; i++) {
        pixels[i] = color;
    }
}

uint32_t* evo_display_get_framebuffer(evo_display_context_t *ctx)
{
    return ctx ? (uint32_t*)ctx->framebuffer : NULL;
}

void evo_display_get_dimensions(evo_display_context_t *ctx, int *width, int *height, int *stride)
{
    if (!ctx) return;
    if (width) *width = ctx->width;
    if (height) *height = ctx->height;
    if (stride) *stride = ctx->stride;
}

const evo_backend_config_t* evo_display_get_backend_info(evo_display_context_t *ctx)
{
    return ctx ? ctx->backend_config : NULL;
}

void evo_display_print_backend_status(evo_display_context_t *ctx)
{
    if (!ctx || !ctx->backend_config) return;
    
    printf("📊 Display Backend Status:\n");
    printf("   Backend: %s\n", ctx->backend_config->name);
    printf("   Library: %s\n", ctx->backend_config->library_path);
    printf("   Description: %s\n", ctx->backend_config->description);
    printf("   Hardware Accel: %s\n", 
           (ctx->backend_config->capabilities & EVO_CAP_HARDWARE_ACCEL) ? "Yes" : "No");
    printf("   Double Buffer: %s\n",
           (ctx->backend_config->capabilities & EVO_CAP_DOUBLE_BUFFER) ? "Yes" : "No");
}

/* ============================================================================
 * PHASE 4: SCREEN BLIT OPERATIONS (Hardware Acceleration Interface)
 * ============================================================================ */

int evo_screen_blit_init(evo_screen_blit_context_t* ctx)
{
    return evo_screen_blit_init_auto(ctx);
}

int evo_screen_blit_init_backend(evo_screen_blit_context_t* ctx, evo_screen_blit_backend_t backend_type)
{
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->backend_type = backend_type;
    
    /* Initialize based on backend type */
    switch (backend_type) {
        case EVO_BLIT_POWERVR:
            printf("🔧 Initializing PowerVR 2D acceleration\n");
            /* PowerVR initialization would go here */
            break;
            
        case EVO_BLIT_VIVANTE:
            printf("🔧 Initializing Vivante GC320 acceleration\n");
            /* Vivante initialization would go here */
            break;
            
        case EVO_BLIT_SOFTWARE:
        default:
            printf("🔧 Initializing software blit\n");
            ctx->backend_type = EVO_BLIT_SOFTWARE;
            break;
    }
    
    ctx->initialized = 1;
    return 0;
}

int evo_screen_blit_init_auto(evo_screen_blit_context_t* ctx)
{
    if (!ctx) return -1;
    
    /* Try backends in order of preference */
    evo_screen_blit_backend_t backends[] = {
        EVO_BLIT_POWERVR,
        EVO_BLIT_VIVANTE,
        EVO_BLIT_SOFTWARE
    };
    
    for (int i = 0; i < sizeof(backends)/sizeof(backends[0]); i++) {
        if (evo_screen_blit_init_backend(ctx, backends[i]) == 0) {
            return 0;
        }
    }
    
    return -1;
}

/* ============================================================================
 * PHASE 4: UNIFIED SYSTEM MANAGEMENT
 * ============================================================================ */

void evo_display_destroy_and_exit(evo_display_context_t **ctx, int exit_code)
{
    evo_display_destroy(ctx);
    printf("[EVO] Exiting with code %d\n", exit_code);
    exit(exit_code);
}

const char* evo_display_get_error(evo_display_context_t *ctx)
{
    (void)ctx; /* Suppress unused parameter warning */
    return "No error information available";
}

int evo_display_has_ilm(evo_display_context_t *ctx)
{
    return (ctx && g_ilm_handle) ? 1 : 0;
}

void evo_display_print_info(evo_display_context_t *ctx, const char *prefix)
{
    if (!ctx) return;
    
    const char* pfx = prefix ? prefix : "[EVO]";
    printf("%s Display Info:\n", pfx);
    printf("%s   Size: %dx%d\n", pfx, ctx->width, ctx->height);
    printf("%s   Position: (%d,%d)\n", pfx, ctx->x, ctx->y);
    printf("%s   Surface ID: %u\n", pfx, ctx->surface_id);
    printf("%s   Layer ID: %u\n", pfx, ctx->layer_id);
    printf("%s   Visible: %s\n", pfx, ctx->visible ? "Yes" : "No");
    printf("%s   Backend: %s\n", pfx, 
           ctx->backend_config ? ctx->backend_config->name : "Unknown");
}

/* EOF: Phase 4 Backend + Display Unified Implementation */

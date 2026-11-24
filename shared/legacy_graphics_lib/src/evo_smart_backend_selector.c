/*
 * EVO Smart Backend Selector Implementation
 * 
 * Fail-hard backend selection with intelligent buffering strategies.
 * Based on real hardware validation results from on-target testing.
 */

#include "evo_smart_backend_selector.h"
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/* ============================================================================
 * BACKEND VALIDATION AND TESTING
 * ============================================================================ */

evo_backend_test_result_t evo_smart_backend_test(evo_smart_backend_type_t backend_type)
{
    evo_backend_test_result_t result = {0};
    
    if (backend_type >= EVO_BACKEND_COUNT) {
        snprintf(result.error_message, sizeof(result.error_message),
                "Invalid backend type: %d", backend_type);
        return result;
    }
    
    const evo_backend_config_t *config = &BACKEND_CONFIGS[backend_type];
    
    /* Test 1: Check if library file exists */
    struct stat st;
    if (stat(config->library_path, &st) != 0) {
        snprintf(result.error_message, sizeof(result.error_message),
                "Backend library not found: %s (%s)", 
                config->library_path, strerror(errno));
        return result;
    }
    result.library_exists = 1;
    
    /* Test 2: Try to load the library */
    void *handle = dlopen(config->library_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        snprintf(result.error_message, sizeof(result.error_message),
                "Backend library not loadable: %s (%s)",
                config->library_path, dlerror());
        return result;
    }
    result.library_loadable = 1;
    
    /* Test 3: Check for basic Screen API symbols */
    if (dlsym(handle, "screen_create_context") &&
        dlsym(handle, "screen_create_window") &&
        dlsym(handle, "screen_create_window_buffers")) {
        result.has_screen_symbols = 1;
    } else {
        snprintf(result.error_message, sizeof(result.error_message),
                "Backend missing Screen API symbols: %s", config->library_path);
    }
    
    dlclose(handle);
    
    if (!result.has_screen_symbols) {
        return result;
    }
    
    /* All tests passed */
    snprintf(result.error_message, sizeof(result.error_message),
            "Backend validation successful");
    return result;
}

void evo_smart_backend_validate_or_die(evo_smart_backend_type_t backend_type)
{
    if (backend_type >= EVO_BACKEND_COUNT) {
        EVO_BACKEND_DIE("Invalid backend type: %d", backend_type);
    }
    
    const evo_backend_config_t *config = &BACKEND_CONFIGS[backend_type];
    evo_backend_test_result_t test = evo_smart_backend_test(backend_type);
    
    if (!test.library_exists) {
        EVO_BACKEND_DIE("Backend %s library missing: %s\n"
                       "    Expected: %s\n"
                       "    Use case: %s\n"
                       "    Requirements: %s",
                       config->name, test.error_message,
                       config->library_path, config->use_case, config->requirements);
    }
    
    if (!test.library_loadable) {
        EVO_BACKEND_DIE("Backend %s not loadable: %s\n"
                       "    This usually means architecture mismatch or missing dependencies\n"
                       "    Requirements: %s",
                       config->name, test.error_message, config->requirements);
    }
    
    if (!test.has_screen_symbols) {
        EVO_BACKEND_DIE("Backend %s missing Screen API: %s\n"
                       "    The library exists but doesn't provide Screen functionality\n"
                       "    Check if this is the correct backend library",
                       config->name, test.error_message);
    }
    
    EVO_BACKEND_SUCCESS("Backend %s validated successfully", config->name);
}

/* ============================================================================
 * SMART BACKEND SELECTION
 * ============================================================================ */

evo_smart_backend_result_t evo_smart_backend_select(const evo_smart_backend_request_t *request)
{
    evo_smart_backend_result_t result = {0};
    
    if (!request) {
        EVO_BACKEND_DIE("Backend selection request is NULL");
    }
    
    /* If specific backend requested, validate it immediately */
    if (request->force_backend) {
        if (request->verbose_logging) {
            EVO_BACKEND_INFO("Forcing backend: %s", 
                           evo_smart_backend_type_to_string(request->backend_type));
        }
        
        /* Validate the forced backend */
        if (request->validate_libraries) {
            evo_smart_backend_validate_or_die(request->backend_type);
        }
        
        result.selected_backend = request->backend_type;
        result.config = &BACKEND_CONFIGS[request->backend_type];
        result.buffer_strategy = result.config->buffer_strategy;
        
        snprintf(result.selection_reason, sizeof(result.selection_reason),
                "Forced selection: %s", result.config->name);
        
        if (request->verbose_logging) {
            EVO_BACKEND_SUCCESS("Backend selection: %s (%s buffering, %s acceleration)",
                              result.config->name,
                              (result.buffer_strategy == EVO_BUFFER_DOUBLE) ? "double" : "single",
                              result.config->is_hardware_accelerated ? "hardware" : "software");
        }
        
        return result;
    }
    
    /* Auto-selection: Try recommended backends first */
    if (request->verbose_logging) {
        EVO_BACKEND_INFO("Auto-selecting optimal backend...");
    }
    
    /* Priority 1: PowerVR-2D (optimal for OMAP5432) */
    evo_backend_test_result_t test = evo_smart_backend_test(EVO_BACKEND_POWERVR_2D);
    if (test.library_exists && test.library_loadable && test.has_screen_symbols) {
        result.selected_backend = EVO_BACKEND_POWERVR_2D;
        result.config = &BACKEND_CONFIGS[EVO_BACKEND_POWERVR_2D];
        result.buffer_strategy = EVO_BUFFER_DOUBLE;
        snprintf(result.selection_reason, sizeof(result.selection_reason),
                "Auto-selected: PowerVR-2D (optimal for OMAP5432)");
        
        if (request->verbose_logging) {
            EVO_BACKEND_SUCCESS("Selected PowerVR-2D: Optimal 2D acceleration");
        }
        return result;
    }
    
    /* Priority 2: OpenGL ES 2.0 (good 3D acceleration) */
    test = evo_smart_backend_test(EVO_BACKEND_GLES2);
    if (test.library_exists && test.library_loadable && test.has_screen_symbols) {
        result.selected_backend = EVO_BACKEND_GLES2;
        result.config = &BACKEND_CONFIGS[EVO_BACKEND_GLES2];
        result.buffer_strategy = EVO_BUFFER_DOUBLE;
        snprintf(result.selection_reason, sizeof(result.selection_reason),
                "Auto-selected: OpenGL ES 2.0 (hardware acceleration)");
        
        if (request->verbose_logging) {
            EVO_BACKEND_SUCCESS("Selected OpenGL ES 2.0: Hardware 3D acceleration");
        }
        return result;
    }
    
    /* Priority 3: Vivante (platform-specific) */
    test = evo_smart_backend_test(EVO_BACKEND_VIVANTE);
    if (test.library_exists && test.library_loadable && test.has_screen_symbols) {
        result.selected_backend = EVO_BACKEND_VIVANTE;
        result.config = &BACKEND_CONFIGS[EVO_BACKEND_VIVANTE];
        result.buffer_strategy = EVO_BUFFER_DOUBLE;
        snprintf(result.selection_reason, sizeof(result.selection_reason),
                "Auto-selected: Vivante OMAP5430 (platform-specific)");
        
        if (request->verbose_logging) {
            EVO_BACKEND_SUCCESS("Selected Vivante: Platform-specific acceleration");
        }
        return result;
    }
    
    /* Priority 4: Basic Screen (reliable but single-buffer only) */
    test = evo_smart_backend_test(EVO_BACKEND_BASIC);
    if (test.library_exists && test.library_loadable && test.has_screen_symbols) {
        result.selected_backend = EVO_BACKEND_BASIC;
        result.config = &BACKEND_CONFIGS[EVO_BACKEND_BASIC];
        result.buffer_strategy = EVO_BUFFER_SINGLE;  /* CRITICAL: Single buffer only! */
        snprintf(result.selection_reason, sizeof(result.selection_reason),
                "Auto-selected: Basic Screen (single buffering only - reliable fallback)");
        
        if (request->verbose_logging) {
            EVO_BACKEND_WARN("Selected Basic Screen: Single buffering only (double buffering causes SIGBUS)");
        }
        return result;
    }
    
    /* No working backends found */
    EVO_BACKEND_DIE("No working graphics backends found!\n"
                   "    Tested: PowerVR-2D, OpenGL ES 2.0, Vivante, Basic Screen\n"
                   "    All backends either missing or non-functional\n"
                   "    Check your QNX Screen backend installation");
    
    /* Never reached, but prevents compiler warning */
    return result;
}

/* ============================================================================
 * DISPLAY CONTEXT CREATION
 * ============================================================================ */

evo_smart_display_context_t* evo_smart_display_create(
    int width, int height, int x, int y,
    const evo_smart_backend_request_t *backend_request)
{
    if (!backend_request) {
        EVO_BACKEND_DIE("Backend request is NULL");
    }
    
    if (width <= 0 || height <= 0) {
        EVO_BACKEND_DIE("Invalid window dimensions: %dx%d", width, height);
    }
    
    /* Select backend with intelligent strategy */
    evo_smart_backend_result_t backend_result = evo_smart_backend_select(backend_request);
    
    /* Allocate context */
    evo_smart_display_context_t *ctx = calloc(1, sizeof(evo_smart_display_context_t));
    if (!ctx) {
        EVO_BACKEND_DIE("Failed to allocate display context");
    }
    
    ctx->backend_info = backend_result;
    ctx->window_width = width;
    ctx->window_height = height;
    
    if (backend_request->verbose_logging) {
        EVO_BACKEND_INFO("Creating display context: %dx%d at (%d,%d)", width, height, x, y);
        EVO_BACKEND_INFO("Backend: %s", backend_result.config->name);
        EVO_BACKEND_INFO("Buffering: %s", 
                        (backend_result.buffer_strategy == EVO_BUFFER_DOUBLE) ? "Double" : "Single");
        EVO_BACKEND_INFO("Reason: %s", backend_result.selection_reason);
    }
    
    /* Create screen context */
    if (screen_create_context(&ctx->screen_ctx, 0) != 0) {
        EVO_BACKEND_DIE("Failed to create screen context with %s backend", 
                       backend_result.config->name);
    }
    
    /* Create window */
    if (screen_create_window(&ctx->screen_win, ctx->screen_ctx) != 0) {
        screen_destroy_context(ctx->screen_ctx);
        free(ctx);
        EVO_BACKEND_DIE("Failed to create screen window with %s backend", 
                       backend_result.config->name);
    }
    
    /* Set window properties */
    int format = SCREEN_FORMAT_RGBA8888;
    int size[2] = {width, height};
    int position[2] = {x, y};
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    
    if (screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_FORMAT, &format) != 0 ||
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_SIZE, size) != 0 ||
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_POSITION, position) != 0 ||
        screen_set_window_property_iv(ctx->screen_win, SCREEN_PROPERTY_USAGE, &usage) != 0) {
        
        screen_destroy_window(ctx->screen_win);
        screen_destroy_context(ctx->screen_ctx);
        free(ctx);
        EVO_BACKEND_DIE("Failed to set window properties with %s backend", 
                       backend_result.config->name);
    }
    
    if (backend_request->verbose_logging) {
        EVO_BACKEND_SUCCESS("Display context created successfully");
    }
    
    return ctx;
}

int evo_smart_display_create_buffers(evo_smart_display_context_t *ctx)
{
    if (!ctx) {
        EVO_BACKEND_DIE("Display context is NULL");
    }
    
    /* Determine buffer count based on backend strategy */
    ctx->buffer_count = (int)ctx->backend_info.buffer_strategy;
    
    if (ctx->backend_info.config->type == EVO_BACKEND_BASIC && ctx->buffer_count > 1) {
        EVO_BACKEND_WARN("Basic Screen backend: Forcing single buffer (double buffering causes SIGBUS)");
        ctx->buffer_count = 1;
        ctx->backend_info.buffer_strategy = EVO_BUFFER_SINGLE;
    }
    
    EVO_BACKEND_INFO("Creating %d buffer(s) for %s backend", 
                    ctx->buffer_count, ctx->backend_info.config->name);
    
    /* Create window buffers */
    if (screen_create_window_buffers(ctx->screen_win, ctx->buffer_count) != 0) {
        EVO_BACKEND_DIE("Failed to create %d buffer(s) for %s backend\n"
                       "    This may indicate incompatible buffer strategy\n"
                       "    Backend: %s\n"
                       "    Strategy: %s buffering",
                       ctx->buffer_count, ctx->backend_info.config->name,
                       ctx->backend_info.config->name,
                       (ctx->backend_info.buffer_strategy == EVO_BUFFER_DOUBLE) ? "Double" : "Single");
    }
    
    /* Get buffer handles */
    ctx->buffers = calloc(ctx->buffer_count, sizeof(screen_buffer_t));
    if (!ctx->buffers) {
        EVO_BACKEND_DIE("Failed to allocate buffer array");
    }
    
    if (screen_get_window_property_pv(ctx->screen_win, SCREEN_PROPERTY_RENDER_BUFFERS, 
                                     (void**)&ctx->buffers) != 0) {
        EVO_BACKEND_DIE("Failed to get render buffers for %s backend", 
                       ctx->backend_info.config->name);
    }
    
    ctx->current_buffer = 0;
    
    EVO_BACKEND_SUCCESS("Created %d buffer(s) successfully", ctx->buffer_count);
    return 0;
}

screen_buffer_t evo_smart_display_get_buffer(evo_smart_display_context_t *ctx)
{
    if (!ctx || !ctx->buffers) {
        EVO_BACKEND_DIE("Display context or buffers not initialized");
    }
    
    return ctx->buffers[ctx->current_buffer];
}

int evo_smart_display_present(evo_smart_display_context_t *ctx)
{
    if (!ctx) {
        EVO_BACKEND_DIE("Display context is NULL");
    }
    
    screen_buffer_t buffer = evo_smart_display_get_buffer(ctx);
    
    /* Post the current buffer */
    if (screen_post_window(ctx->screen_win, buffer, 1, NULL, 0) != 0) {
        EVO_BACKEND_DIE("Failed to post buffer for %s backend", 
                       ctx->backend_info.config->name);
    }
    
    /* Flush context to ensure rendering */
    if (screen_flush_context(ctx->screen_ctx, 0) != 0) {
        EVO_BACKEND_WARN("Failed to flush context for %s backend", 
                        ctx->backend_info.config->name);
    }
    
    /* For double buffering, switch to next buffer */
    if (ctx->backend_info.buffer_strategy == EVO_BUFFER_DOUBLE) {
        ctx->current_buffer = (ctx->current_buffer + 1) % ctx->buffer_count;
    }
    /* For single buffering, keep using the same buffer */
    
    return 0;
}

void evo_smart_display_destroy(evo_smart_display_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    
    EVO_BACKEND_INFO("Destroying display context (%s backend)", 
                    ctx->backend_info.config->name);
    
    if (ctx->buffers) {
        free(ctx->buffers);
    }
    
    if (ctx->screen_win) {
        screen_destroy_window(ctx->screen_win);
    }
    
    if (ctx->screen_ctx) {
        screen_destroy_context(ctx->screen_ctx);
    }
    
    free(ctx);
    EVO_BACKEND_SUCCESS("Display context destroyed");
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

const evo_backend_config_t* evo_smart_backend_get_config(evo_smart_backend_type_t type)
{
    if (type >= EVO_BACKEND_COUNT) {
        return NULL;
    }
    return &BACKEND_CONFIGS[type];
}

void evo_smart_backend_list_all(void)
{
    printf("\n🔍 EVO Smart Backend Analysis\n");
    printf("============================\n\n");
    
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        const evo_backend_config_t *config = &BACKEND_CONFIGS[i];
        evo_backend_test_result_t test = evo_smart_backend_test(i);
        
        printf("%s %s:\n", config->is_recommended ? "⭐" : "🔧", config->name);
        printf("   Library: %s\n", config->library_path);
        printf("   Strategy: %s buffering\n", 
               (config->buffer_strategy == EVO_BUFFER_DOUBLE) ? "Double" : "Single");
        printf("   Acceleration: %s\n", config->is_hardware_accelerated ? "Hardware" : "Software");
        printf("   Use case: %s\n", config->use_case);
        printf("   Status: ");
        
        if (!test.library_exists) {
            printf("❌ Library missing\n");
        } else if (!test.library_loadable) {
            printf("❌ Not loadable\n");
        } else if (!test.has_screen_symbols) {
            printf("❌ Missing Screen API\n");
        } else {
            printf("✅ Available\n");
        }
        printf("\n");
    }
}

const char* evo_smart_backend_type_to_string(evo_smart_backend_type_t type)
{
    const evo_backend_config_t *config = evo_smart_backend_get_config(type);
    return config ? config->name : "Unknown";
}

evo_smart_backend_type_t evo_smart_backend_string_to_type(const char *name)
{
    if (!name) {
        EVO_BACKEND_DIE("Backend name is NULL");
    }
    
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        if (strcasecmp(name, BACKEND_CONFIGS[i].name) == 0) {
            return (evo_smart_backend_type_t)i;
        }
    }
    
    EVO_BACKEND_DIE("Unknown backend name: '%s'\n"
                   "    Available backends: PowerVR-2D, OpenGL ES 2.0, OpenGL ES 1.0, Vivante OMAP5430, Basic Screen, Software",
                   name);
    
    /* Never reached */
    return EVO_BACKEND_POWERVR_2D;
}

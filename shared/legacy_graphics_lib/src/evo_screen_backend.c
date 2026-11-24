/*
 * EVO Screen Backend Management Implementation
 * 
 * Multi-backend support for QNX Screen API with automatic backend
 * selection and capability testing.
 */

#include "evo_screen_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

/* ============================================================================
 * BACKEND INFORMATION DATABASE
 * ============================================================================ */

static evo_backend_info_t g_backend_info[] = {
    {
        .type = EVO_BACKEND_BASIC,
        .name = "Basic",
        .description = "Basic QNX Screen API (libscreen.so.1 only)",
        .library_name = "libscreen.so.1",
        .library_path = "/usr/lib/libscreen.so.1",
        .capabilities = EVO_CAP_SINGLE_BUFFER,
        .priority = 10,  /* Low priority - fallback only */
        .display_support = 0x03,  /* Both displays */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_PVR2D,
        .name = "PowerVR 2D",
        .description = "PowerVR SGX540 2D acceleration (screen-pvr2d.so)",
        .library_name = "screen-pvr2d.so",
        .library_path = "/usr/lib/graphics/screen-pvr2d.so",
        .capabilities = EVO_CAP_SINGLE_BUFFER | EVO_CAP_DOUBLE_BUFFER | 
                       EVO_CAP_VSYNC | EVO_CAP_HARDWARE_ACCEL | EVO_CAP_BUFFER_PROPS,
        .priority = 90,  /* High priority for primary display */
        .display_support = 0x01,  /* Primary display only (1440x540) */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_GLES2,
        .name = "OpenGL ES 2.0",
        .description = "OpenGL ES 2.0 screen backend (screen-gles2.so)",
        .library_name = "screen-gles2.so",
        .library_path = "/usr/lib/graphics/screen-gles2.so",
        .capabilities = EVO_CAP_SINGLE_BUFFER | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_TRIPLE_BUFFER |
                       EVO_CAP_VSYNC | EVO_CAP_HARDWARE_ACCEL | EVO_CAP_EGL_CONTEXT |
                       EVO_CAP_OPENGL_ES2 | EVO_CAP_BUFFER_PROPS | EVO_CAP_ADVANCED_BLEND,
        .priority = 85,  /* High priority for 3D applications */
        .display_support = 0x01,  /* Primary display */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_GLES1,
        .name = "OpenGL ES 1.0",
        .description = "OpenGL ES 1.0 screen backend (screen-gles1.so)",
        .library_name = "screen-gles1.so", 
        .library_path = "/usr/lib/graphics/screen-gles1.so",
        .capabilities = EVO_CAP_SINGLE_BUFFER | EVO_CAP_DOUBLE_BUFFER |
                       EVO_CAP_VSYNC | EVO_CAP_HARDWARE_ACCEL | EVO_CAP_EGL_CONTEXT |
                       EVO_CAP_OPENGL_ES1 | EVO_CAP_BUFFER_PROPS,
        .priority = 70,  /* Medium priority */
        .display_support = 0x01,  /* Primary display */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_VIVANTE,
        .name = "Vivante GC320",
        .description = "Vivante GC320 2D acceleration (screen-vivante-omap5430.so)",
        .library_name = "screen-vivante-omap5430.so",
        .library_path = "/usr/lib/graphics/screen-vivante-omap5430.so",
        .capabilities = EVO_CAP_SINGLE_BUFFER | EVO_CAP_DOUBLE_BUFFER |
                       EVO_CAP_VSYNC | EVO_CAP_HARDWARE_ACCEL | EVO_CAP_BUFFER_PROPS,
        .priority = 80,  /* High priority for secondary display */
        .display_support = 0x02,  /* Secondary display only (800x480) */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_SOFTWARE,
        .name = "Software",
        .description = "Software-only rendering (screen-sw.so)",
        .library_name = "screen-sw.so",
        .library_path = "/usr/lib/graphics/screen-sw.so",
        .capabilities = EVO_CAP_SINGLE_BUFFER | EVO_CAP_DOUBLE_BUFFER,
        .priority = 20,  /* Low priority - software fallback */
        .display_support = 0x03,  /* Both displays */
        .tested = 0,
        .available = 0,
        .functional = 0,
        .last_error = NULL
    }
};

static const int g_backend_count = sizeof(g_backend_info) / sizeof(g_backend_info[0]);
static evo_screen_backend_t g_backend_instances[EVO_BACKEND_COUNT];
static int g_backend_system_initialized = 0;

/* ============================================================================
 * BACKEND SYSTEM INITIALIZATION
 * ============================================================================ */

int evo_backend_system_init(void) {
    if (g_backend_system_initialized) {
        return 0;  /* Already initialized */
    }
    
    printf("🔧 Initializing EVO Screen Backend Management System\n");
    
    /* Clear backend instances */
    memset(g_backend_instances, 0, sizeof(g_backend_instances));
    
    /* Test availability of all backends */
    for (int i = 0; i < g_backend_count; i++) {
        evo_backend_test_availability(g_backend_info[i].type);
    }
    
    g_backend_system_initialized = 1;
    printf("✅ Backend system initialized\n");
    return 0;
}

void evo_backend_system_shutdown(void) {
    if (!g_backend_system_initialized) {
        return;
    }
    
    printf("🔧 Shutting down backend system\n");
    
    /* Release all backend instances */
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        if (g_backend_instances[i].initialized) {
            evo_backend_release_instance(&g_backend_instances[i]);
        }
    }
    
    g_backend_system_initialized = 0;
    printf("✅ Backend system shutdown complete\n");
}

/* ============================================================================
 * BACKEND INFORMATION FUNCTIONS
 * ============================================================================ */

const evo_backend_info_t* evo_backend_get_info_list(int *count) {
    if (count) {
        *count = g_backend_count;
    }
    return g_backend_info;
}

const evo_backend_info_t* evo_backend_get_info(evo_screen_backend_type_t type) {
    if (type <= EVO_BACKEND_AUTO || type >= EVO_BACKEND_COUNT) {
        return NULL;
    }
    
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backend_info[i].type == type) {
            return &g_backend_info[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * BACKEND TESTING FUNCTIONS
 * ============================================================================ */

int evo_backend_test_availability(evo_screen_backend_type_t type) {
    evo_backend_info_t *info = NULL;
    
    /* Find backend info */
    for (int i = 0; i < g_backend_count; i++) {
        if (g_backend_info[i].type == type) {
            info = &g_backend_info[i];
            break;
        }
    }
    
    if (!info) {
        return -1;
    }
    
    info->tested = 1;
    info->available = 0;
    info->functional = 0;
    info->last_error = NULL;
    
    printf("🧪 Testing backend: %s (%s)\n", info->name, info->library_name);
    
    /* Test 1: Check if library file exists */
    if (access(info->library_path, R_OK) != 0) {
        /* Try alternative paths */
        char alt_path[256];
        snprintf(alt_path, sizeof(alt_path), "/lib/graphics/%s", info->library_name);
        if (access(alt_path, R_OK) == 0) {
            printf("✓ Found library at alternative path: %s\n", alt_path);
        } else {
            snprintf(alt_path, sizeof(alt_path), "/fs/sda0/lib/graphics/%s", info->library_name);
            if (access(alt_path, R_OK) == 0) {
                printf("✓ Found library at target path: %s\n", alt_path);
            } else {
                info->last_error = "Library file not found";
                printf("❌ Library not found: %s\n", info->library_path);
                return -1;
            }
        }
    } else {
        printf("✓ Library found: %s\n", info->library_path);
    }
    
    info->available = 1;
    
    /* Test 2: Try loading the library (for non-basic backends) */
    if (type != EVO_BACKEND_BASIC) {
        void *handle = dlopen(info->library_path, RTLD_LAZY | RTLD_NOLOAD);
        if (handle) {
            printf("✓ Library already loaded\n");
            dlclose(handle);
            info->functional = 1;
        } else {
            /* Try to load it */
            handle = dlopen(info->library_path, RTLD_LAZY);
            if (handle) {
                printf("✓ Library loads successfully\n");
                dlclose(handle);
                info->functional = 1;
            } else {
                info->last_error = dlerror();
                printf("⚠️ Library load failed: %s\n", info->last_error);
                /* Still mark as available since file exists */
                info->functional = 0;
            }
        }
    } else {
        /* Basic backend - always functional if available */
        info->functional = 1;
    }
    
    printf("📊 Backend %s: available=%s, functional=%s\n", 
           info->name, 
           info->available ? "YES" : "NO",
           info->functional ? "YES" : "NO");
    
    return info->available ? 0 : -1;
}

/* ============================================================================
 * BACKEND SELECTION FUNCTIONS
 * ============================================================================ */

evo_screen_backend_type_t evo_backend_select_best(int display_id, 
                                                  int require_double_buffer,
                                                  int require_hardware_accel) {
    int best_score = -1;
    evo_screen_backend_type_t best_backend = EVO_BACKEND_BASIC;
    
    printf("🎯 Selecting best backend for display %d (double_buffer=%s, hw_accel=%s)\n",
           display_id,
           require_double_buffer ? "required" : "optional",
           require_hardware_accel ? "required" : "optional");
    
    for (int i = 0; i < g_backend_count; i++) {
        const evo_backend_info_t *info = &g_backend_info[i];
        
        /* Skip if not available */
        if (!info->available) {
            printf("⏭️  Skipping %s: not available\n", info->name);
            continue;
        }
        
        /* Check display support */
        if (!(info->display_support & (1 << display_id))) {
            printf("⏭️  Skipping %s: doesn't support display %d\n", info->name, display_id);
            continue;
        }
        
        /* Check double buffer requirement */
        if (require_double_buffer && !(info->capabilities & EVO_CAP_DOUBLE_BUFFER)) {
            printf("⏭️  Skipping %s: doesn't support double buffering\n", info->name);
            continue;
        }
        
        /* Check hardware acceleration requirement */
        if (require_hardware_accel && !(info->capabilities & EVO_CAP_HARDWARE_ACCEL)) {
            printf("⏭️  Skipping %s: doesn't support hardware acceleration\n", info->name);
            continue;
        }
        
        /* Calculate score */
        int score = info->priority;
        
        /* Bonus for functional backends */
        if (info->functional) {
            score += 10;
        }
        
        /* Bonus for having requested capabilities */
        if (require_double_buffer && (info->capabilities & EVO_CAP_DOUBLE_BUFFER)) {
            score += 5;
        }
        
        if (require_hardware_accel && (info->capabilities & EVO_CAP_HARDWARE_ACCEL)) {
            score += 5;
        }
        
        printf("📊 Backend %s: score=%d (priority=%d, functional=%s)\n",
               info->name, score, info->priority, info->functional ? "yes" : "no");
        
        if (score > best_score) {
            best_score = score;
            best_backend = info->type;
        }
    }
    
    const evo_backend_info_t *selected = evo_backend_get_info(best_backend);
    printf("🏆 Selected backend: %s (score=%d)\n", 
           selected ? selected->name : "unknown", best_score);
    
    return best_backend;
}

/* ============================================================================
 * BACKEND INSTANCE MANAGEMENT
 * ============================================================================ */

evo_screen_backend_t* evo_backend_get_instance(evo_screen_backend_type_t type) {
    if (type <= EVO_BACKEND_AUTO || type >= EVO_BACKEND_COUNT) {
        return NULL;
    }
    
    evo_screen_backend_t *instance = &g_backend_instances[type];
    
    if (!instance->initialized) {
        const evo_backend_info_t *info = evo_backend_get_info(type);
        if (!info) {
            return NULL;
        }
        
        instance->type = type;
        instance->info = info;
        instance->library_handle = NULL;
        instance->context_count = 0;
        instance->initialized = 1;
        
        printf("✅ Backend instance created: %s\n", info->name);
    }
    
    instance->context_count++;
    return instance;
}

void evo_backend_release_instance(evo_screen_backend_t *backend) {
    if (!backend || !backend->initialized) {
        return;
    }
    
    backend->context_count--;
    
    if (backend->context_count <= 0) {
        if (backend->library_handle) {
            dlclose(backend->library_handle);
            backend->library_handle = NULL;
        }
        
        printf("✅ Backend instance released: %s\n", backend->info->name);
        backend->initialized = 0;
    }
}

/* ============================================================================
 * BACKEND CONTEXT CREATION
 * ============================================================================ */

int evo_backend_create_context(screen_context_t *ctx, 
                              const evo_backend_context_config_t *config,
                              evo_screen_backend_t **selected_backend) {
    if (!ctx || !config) {
        return -1;
    }
    
    evo_screen_backend_type_t backend_type = config->preferred_backend;
    
    /* Auto-select backend if requested */
    if (backend_type == EVO_BACKEND_AUTO) {
        backend_type = evo_backend_select_best(config->display_id,
                                              config->require_double_buffer,
                                              config->require_hardware_accel);
    }
    
    /* Get backend instance */
    evo_screen_backend_t *backend = evo_backend_get_instance(backend_type);
    if (!backend) {
        printf("❌ Failed to get backend instance for type %d\n", backend_type);
        return -1;
    }
    
    if (config->verbose_logging) {
        printf("🔧 Creating screen context with backend: %s\n", backend->info->name);
    }
    
    /* Create screen context using standard API */
    int rc = screen_create_context(ctx, 0);
    if (rc != 0) {
        printf("❌ Failed to create screen context (rc=%d)\n", rc);
        evo_backend_release_instance(backend);
        return -1;
    }
    
    if (config->verbose_logging) {
        printf("✅ Screen context created successfully with %s backend\n", backend->info->name);
    }
    
    if (selected_backend) {
        *selected_backend = backend;
    }
    
    return 0;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

const char* evo_backend_type_to_string(evo_screen_backend_type_t type) {
    const evo_backend_info_t *info = evo_backend_get_info(type);
    return info ? info->name : "Unknown";
}

evo_screen_backend_type_t evo_backend_string_to_type(const char *str) {
    if (!str) return EVO_BACKEND_AUTO;
    
    for (int i = 0; i < g_backend_count; i++) {
        if (strcasecmp(str, g_backend_info[i].name) == 0) {
            return g_backend_info[i].type;
        }
    }
    return EVO_BACKEND_AUTO;
}

const char* evo_backend_capabilities_to_string(int capabilities) {
    static char buffer[512];
    buffer[0] = '\0';
    
    if (capabilities & EVO_CAP_SINGLE_BUFFER) strcat(buffer, "SingleBuf ");
    if (capabilities & EVO_CAP_DOUBLE_BUFFER) strcat(buffer, "DoubleBuf ");
    if (capabilities & EVO_CAP_TRIPLE_BUFFER) strcat(buffer, "TripleBuf ");
    if (capabilities & EVO_CAP_VSYNC) strcat(buffer, "VSync ");
    if (capabilities & EVO_CAP_HARDWARE_ACCEL) strcat(buffer, "HWAccel ");
    if (capabilities & EVO_CAP_EGL_CONTEXT) strcat(buffer, "EGL ");
    if (capabilities & EVO_CAP_OPENGL_ES1) strcat(buffer, "GLES1 ");
    if (capabilities & EVO_CAP_OPENGL_ES2) strcat(buffer, "GLES2 ");
    if (capabilities & EVO_CAP_BUFFER_PROPS) strcat(buffer, "BufProps ");
    if (capabilities & EVO_CAP_ADVANCED_BLEND) strcat(buffer, "AdvBlend ");
    
    return buffer;
}

void evo_backend_print_info_table(void) {
    printf("\n📋 EVO Screen Backend Information Table\n");
    printf("=======================================================================\n");
    printf("%-15s %-8s %-8s %-4s %-12s %s\n", 
           "Backend", "Available", "Functional", "Prio", "Displays", "Capabilities");
    printf("-----------------------------------------------------------------------\n");
    
    for (int i = 0; i < g_backend_count; i++) {
        const evo_backend_info_t *info = &g_backend_info[i];
        char displays[16];
        snprintf(displays, sizeof(displays), "%s%s", 
                 (info->display_support & 0x01) ? "0" : "",
                 (info->display_support & 0x02) ? "1" : "");
        
        printf("%-15s %-8s %-8s %-4d %-12s %s\n",
               info->name,
               info->tested ? (info->available ? "YES" : "NO") : "untested",
               info->tested ? (info->functional ? "YES" : "NO") : "unknown",
               info->priority,
               displays,
               evo_backend_capabilities_to_string(info->capabilities));
        
        if (info->last_error) {
            printf("    Error: %s\n", info->last_error);
        }
    }
    printf("=======================================================================\n\n");
}

int evo_backend_run_tests(int verbose) {
    printf("🧪 Running comprehensive backend tests\n");
    
    evo_backend_system_init();
    
    int passed = 0, total = 0;
    
    for (int i = 0; i < g_backend_count; i++) {
        total++;
        if (evo_backend_test_availability(g_backend_info[i].type) == 0) {
            passed++;
        }
    }
    
    if (verbose) {
        evo_backend_print_info_table();
    }
    
    printf("📊 Backend test results: %d/%d passed\n", passed, total);
    return passed;
}

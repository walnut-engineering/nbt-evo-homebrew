/**
 * @file evo_backend_simple.c
 * @brief EVO Simplified Backend Implementation following backend.prompt.yaml HARD-FAIL rules
 * 
 * This implementation follows the successful July 25th pattern:
 * - Direct gpu_init() call
 * - Simple gco2D_Construct context creation
 * - HARD-FAIL on any error (no software fallbacks)
 */

#include "../include/evo_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>

/* Global backend state */
static int g_backend_initialized = 0;

/* Global GAL 2D context - created during early initialization */
static void* g_global_gco2d_context = NULL;

/* ============================================================================
 * SIMPLIFIED BACKEND INITIALIZATION - HARD-FAIL APPROACH
 * ============================================================================ */

int evo_backend_init(void)
{
    if (g_backend_initialized) {
        return 0;
    }
    
    printf("🔧 Backend system initialized\n");
    
    /* CRITICAL: Simple GAL initialization following July 25th successful pattern */
    printf("🚀 [Simple Init] Direct GAL initialization - HARD-FAIL approach...\n");
    
    /* Load Galcore library directly for simple gpu_init call */
    void* galcore_lib = dlopen("/fs/sda0/lib/graphics/omap5430_ES2_0/libGalcore.so", RTLD_LAZY);
    if (!galcore_lib) {
        galcore_lib = dlopen("/fs/sda0/lib/libGalcore.so", RTLD_LAZY);
    }
    if (!galcore_lib) {
        galcore_lib = dlopen("/lib/libGalcore.so", RTLD_LAZY);
    }
    
    if (!galcore_lib) {
        printf("❌ [Simple Init] HARD-FAIL: Galcore library not found\n");
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    printf("✅ [Simple Init] Galcore library loaded successfully\n");
    
    /* Direct gpu_init call - matching successful July 25th pattern */
    typedef int (*gpu_init_t)(void);
    gpu_init_t gpu_init = (gpu_init_t)dlsym(galcore_lib, "gpu_init");
    
    if (!gpu_init) {
        printf("❌ [Simple Init] HARD-FAIL: gpu_init function not found\n");
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    printf("🔧 [Simple Init] CRITICAL: Direct gpu_init() call...\n");
    int result = gpu_init();
    printf("   gpu_init() result: %d\n", result);
    
    if (result != 0) {
        printf("❌ [Simple Init] HARD-FAIL: gpu_init() failed with result: %d\n", result);
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    printf("✅ [Simple Init] SUCCESS: GPU hardware initialized (July 25th pattern)!\n");
    
    /* Create gco2D context immediately after successful hardware init */
    void* gal_lib = dlopen("/fs/sda0/lib/libGAL-vivante-omap5430.so.1", RTLD_LAZY);
    if (!gal_lib) {
        gal_lib = dlopen("libGAL-vivante-omap5430.so.1", RTLD_LAZY);  /* else rely on the loader search path (LD_LIBRARY_PATH) */
    }
    
    if (!gal_lib) {
        printf("❌ [Simple Init] HARD-FAIL: GAL library not found\n");
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    typedef int (*gco2D_Construct_t)(void*, void**);
    gco2D_Construct_t gco2D_Construct = (gco2D_Construct_t)dlsym(gal_lib, "gco2D_Construct");
    
    if (!gco2D_Construct) {
        printf("❌ [Simple Init] HARD-FAIL: gco2D_Construct function not found\n");
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    int ctx_result = gco2D_Construct(NULL, &g_global_gco2d_context);
    printf("   gco2D_Construct result: %d, context: %p\n", ctx_result, g_global_gco2d_context);
    
    if (ctx_result != 0 || !g_global_gco2d_context) {
        printf("❌ [Simple Init] HARD-FAIL: gco2D_Construct failed: %d\n", ctx_result);
        return -1;  // HARD-FAIL per backend.prompt.yaml rules
    }
    
    printf("✅ [Simple Init] SUCCESS: 2D context created, GAL system ready!\n");
    
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
 * BACKEND CONFIGURATION DATABASE
 * ============================================================================ */

static const evo_backend_config_t BACKEND_CONFIGS[EVO_BACKEND_COUNT] = {
    {
        .type = EVO_BACKEND_POWERVR_2D,
        .name = "PowerVR-2D",
        .library_path = "/lib/screen-pvr2d.so",
        .buffer_strategy = EVO_BUFFER_DOUBLE,
        .is_hardware_accelerated = 1,
        .is_recommended = 1,
        .use_case = "Optimal 2D performance on OMAP5432",
        .requirements = "PowerVR SGX GPU, OMAP5432 platform"
    },
    {
        .type = EVO_BACKEND_GLES2,
        .name = "OpenGL ES 2.0",
        .library_path = "/lib/screen-gles2.so",
        .buffer_strategy = EVO_BUFFER_DOUBLE,
        .is_hardware_accelerated = 1,
        .is_recommended = 1,
        .use_case = "3D graphics, shader-based rendering",
        .requirements = "OpenGL ES 2.0 compatible GPU"
    },
    {
        .type = EVO_BACKEND_VIVANTE_2D,
        .name = "Vivante-2D",
        .library_path = "/lib/screen-vivante-omap5430.so",
        .buffer_strategy = EVO_BUFFER_DOUBLE,
        .is_hardware_accelerated = 1,
        .is_recommended = 1,
        .use_case = "2D hardware acceleration via Vivante GAL",
        .requirements = "Vivante Galcore, GAL library"
    },
    {
        .type = EVO_BACKEND_SOFTWARE,
        .name = "Software",
        .library_path = "/lib/screen-sw.so",
        .buffer_strategy = EVO_BUFFER_SINGLE,
        .is_hardware_accelerated = 0,
        .is_recommended = 0,
        .use_case = "Fallback CPU rendering",
        .requirements = "None"
    }
};

/* ============================================================================
 * BACKEND SELECTION AND MANAGEMENT
 * ============================================================================ */

const evo_backend_config_t* evo_backend_get_config(evo_backend_type_t type)
{
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        if (BACKEND_CONFIGS[i].type == type) {
            return &BACKEND_CONFIGS[i];
        }
    }
    return NULL;
}

const evo_backend_config_t* evo_backend_get_recommended(void)
{
    /* Return first recommended backend - prioritize hardware acceleration */
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        if (BACKEND_CONFIGS[i].is_recommended && BACKEND_CONFIGS[i].is_hardware_accelerated) {
            return &BACKEND_CONFIGS[i];
        }
    }
    
    /* Fallback to any recommended backend */
    for (int i = 0; i < EVO_BACKEND_COUNT; i++) {
        if (BACKEND_CONFIGS[i].is_recommended) {
            return &BACKEND_CONFIGS[i];
        }
    }
    
    return &BACKEND_CONFIGS[0];  // Return first backend as last resort
}

int evo_backend_get_count(void)
{
    return EVO_BACKEND_COUNT;
}

const evo_backend_config_t* evo_backend_get_all_configs(void)
{
    return BACKEND_CONFIGS;
}

/* ============================================================================
 * BACKEND CAPABILITY QUERIES
 * ============================================================================ */

int evo_backend_supports_hardware_acceleration(evo_backend_type_t type)
{
    const evo_backend_config_t* config = evo_backend_get_config(type);
    return config ? config->is_hardware_accelerated : 0;
}

int evo_backend_is_available(evo_backend_type_t type)
{
    const evo_backend_config_t* config = evo_backend_get_config(type);
    if (!config) {
        return 0;
    }
    
    /* Check if library exists */
    void* lib = dlopen(config->library_path, RTLD_LAZY);
    if (lib) {
        dlclose(lib);
        return 1;
    }
    
    return 0;
}

/* ============================================================================
 * VIVANTE GAL CONTEXT ACCESS
 * ============================================================================ */

void* evo_backend_get_gal_context(void)
{
    return g_global_gco2d_context;
}

int evo_backend_has_gal_context(void)
{
    return (g_global_gco2d_context != NULL) ? 1 : 0;
}

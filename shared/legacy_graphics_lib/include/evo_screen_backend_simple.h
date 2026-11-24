/*
 * EVO Screen Backend Management - Simplified for Testing
 * 
 * This simplified header provides just what we need to test different
 * screen backends and learn their capabilities.
 */

#ifndef EVO_SCREEN_BACKEND_SIMPLE_H
#define EVO_SCREEN_BACKEND_SIMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * BACKEND TYPE DEFINITIONS
 * ============================================================================ */

/**
 * Available screen backends for OMAP5430 system
 */
typedef enum {
    EVO_BACKEND_AUTO = 0,           /* Auto-detect best backend */
    EVO_BACKEND_BASIC,              /* libscreen.so.1 only (basic functionality) */
    EVO_BACKEND_PVR2D,              /* screen-pvr2d.so (PowerVR SGX540) */
    EVO_BACKEND_GLES2,              /* screen-gles2.so (OpenGL ES 2.0) */
    EVO_BACKEND_GLES1,              /* screen-gles1.so (OpenGL ES 1.0) */
    EVO_BACKEND_VIVANTE,            /* screen-vivante-omap5430.so (Vivante GC320) */
    EVO_BACKEND_SOFTWARE,           /* screen-sw.so (software rendering) */
    EVO_BACKEND_COUNT
} evo_backend_type_t;

/**
 * Backend capability flags
 */
typedef enum {
    EVO_CAP_DISPLAY         = 0x01,  /* Basic display support */
    EVO_CAP_DOUBLE_BUFFER   = 0x02,  /* Double buffer support */
    EVO_CAP_HW_ACCEL        = 0x04,  /* Hardware acceleration */
    EVO_CAP_GLES2           = 0x08,  /* OpenGL ES 2.0 support */
    EVO_CAP_GLES1           = 0x10,  /* OpenGL ES 1.0 support */
    EVO_CAP_2D_ACCEL        = 0x20,  /* 2D acceleration */
    EVO_CAP_VSYNC           = 0x40,  /* VSync support */
    EVO_CAP_BUFFER_PROPS    = 0x80,  /* Buffer property access */
} evo_backend_capabilities_t;

/**
 * Backend information structure
 */
typedef struct {
    evo_backend_type_t type;
    const char *name;
    const char *description;
    const char *library_name;
    evo_backend_capabilities_t capabilities;
    int priority;                           /* Higher = better (0-100) */
    int display_support;                    /* Boolean: supports display output */
    const char *last_error;                 /* Last error message */
} evo_backend_info_t;

/* ============================================================================
 * SIMPLE BACKEND MANAGEMENT FUNCTIONS
 * ============================================================================ */

/**
 * Get backend information by type
 */
evo_backend_info_t evo_backend_get_info(evo_backend_type_t type);

/**
 * Get backend name as string
 */
const char* evo_backend_get_name(evo_backend_type_t type);

/**
 * Test if a specific backend is available on the system
 */
int evo_backend_test_availability(evo_backend_type_t type);

/**
 * Select the best available backend
 */
evo_backend_type_t evo_backend_select_best(void);

/**
 * Print information about all backends
 */
void evo_backend_print_all_info(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_SCREEN_BACKEND_SIMPLE_H */

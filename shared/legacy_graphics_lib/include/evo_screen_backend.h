/*
 * EVO Screen Backend Management - Multi-Backend Support
 * 
 * This header defines the screen backend abstraction layer that allows
 * the EVO display system to work with different QNX Screen backends
 * (basic, PowerVR, OpenGL ES, Vivante) based on system capabilities.
 */

#ifndef EVO_SCREEN_BACKEND_H
#define EVO_SCREEN_BACKEND_H

#include <screen/screen.h>

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
    const char *library_path;
    int capabilities;                   /* evo_backend_capabilities_t flags */
    int priority;                       /* Higher = preferred (0-100) */
    int display_support;                /* Bitmask: bit 0=display 0, bit 1=display 1 */
    int tested;                         /* Has this backend been tested? */
    int available;                      /* Is this backend available? */
    int functional;                     /* Does this backend work? */
    const char *last_error;             /* Last error message */
} evo_backend_info_t;

/**
 * Backend instance structure
 */
typedef struct {
    evo_backend_type_t type;
    const evo_backend_info_t *info;
    void *library_handle;               /* dlopen handle */
    int initialized;
    int context_count;                  /* Number of active contexts */
    
    /* Backend-specific function pointers can be added here */
    /* For now, we use standard Screen API calls */
} evo_backend_t;

/* ============================================================================
 * BACKEND MANAGEMENT FUNCTIONS
 * ============================================================================ */

/**
 * Initialize the backend management system
 */
int evo_backend_system_init(void);

/**
 * Shutdown the backend management system
 */
void evo_backend_system_shutdown(void);

/**
 * Get information about all available backends
 */
const evo_backend_info_t* evo_backend_get_info_list(int *count);

/**
 * Get information about a specific backend
 */
const evo_backend_info_t* evo_backend_get_info(evo_backend_type_t type);

/**
 * Test if a specific backend is available on the system
 */
int evo_backend_test_availability(evo_backend_type_t type);

/**
 * Select the best available backend for a given display
 */
evo_backend_type_t evo_backend_select_best(int display_id, 
                                          evo_backend_capabilities_t required_caps);

/**
 * Get backend instance for manual management
 */
evo_backend_t* evo_backend_get_instance(evo_backend_type_t type);

/**
 * Release backend instance
 */
void evo_backend_release_instance(evo_screen_backend_t *backend);

/* ============================================================================
 * BACKEND-SPECIFIC CONTEXT CREATION
 * ============================================================================ */

/**
 * Enhanced context creation with backend selection
 */
typedef struct {
    evo_backend_type_t preferred_backend;
    evo_backend_type_t fallback_backend;
    int allow_fallback;
    int require_double_buffer;
    int require_hardware_accel;
    int display_id;                     /* 0=primary (1440x540), 1=secondary (800x480) */
    int verbose_logging;
} evo_backend_context_config_t;

/**
 * Create screen context with backend selection
 */
int evo_backend_create_context(screen_context_t *ctx, 
                              const evo_backend_context_config_t *config,
                              evo_screen_backend_t **selected_backend);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Convert backend type to string
 */
const char* evo_backend_type_to_string(evo_backend_type_t type);

/**
 * Convert string to backend type
 */
evo_backend_type_t evo_backend_string_to_type(const char *str);

/**
 * Get capabilities as human-readable string
 */
const char* evo_backend_capabilities_to_string(int capabilities);

/**
 * Print backend information table
 */
void evo_backend_print_info_table(void);

/**
 * Run comprehensive backend tests
 */
int evo_backend_run_tests(int verbose);

#ifdef __cplusplus
}
#endif

#endif /* EVO_SCREEN_BACKEND_H */

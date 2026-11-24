/**
 * @file evo_backend.h
 * @brief EVO Unified Backend Management
 * 
 * This module provides intelligent backend selection with fail-hard validation.
 * Combines the best features from all previous backend systems.
 * 
 * Key Features:
 * - FAIL HARD: No silent fallbacks that hide configuration problems
 * - AUTO SELECTION: Intelligent backend selection based on hardware
 * - BACKEND-SPECIFIC BUFFERING: Hardware backends use double buffering
 * - CLEAR ERROR MESSAGES: Immediate feedback on misconfigurations
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_BACKEND_H
#define EVO_BACKEND_H

#include <screen/screen.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * BACKEND TYPES AND CAPABILITIES
 * ============================================================================ */

/**
 * Available backend types
 */
typedef enum {
    EVO_BACKEND_AUTO = -1,         /* Auto-select best available backend */
    EVO_BACKEND_POWERVR_2D = 0,    /* Hardware-accelerated 2D (recommended) */
    EVO_BACKEND_GLES2,             /* OpenGL ES 2.0 hardware acceleration */
    EVO_BACKEND_GLES1,             /* OpenGL ES 1.0 legacy hardware */
    EVO_BACKEND_VIVANTE,           /* Vivante GPU for OMAP5430 */
    EVO_BACKEND_GAL,               /* GAL Vivante 2D Hardware Acceleration */
    EVO_BACKEND_BASIC,             /* Basic libscreen.so.1 (single buffer only) */
    EVO_BACKEND_SOFTWARE,          /* Software rendering (usually broken) */
    EVO_BACKEND_COUNT
} evo_backend_type_t;

/**
 * Buffer strategies for different backends
 */
typedef enum {
    EVO_BUFFER_SINGLE = 1,         /* Single buffering (Basic backend) */
    EVO_BUFFER_DOUBLE = 2          /* Double buffering (Hardware backends) */
} evo_buffer_strategy_t;

/* ============================================================================
 * SCREEN BLIT MODULE INTEGRATION
 * ============================================================================ */

/**
 * Screen blit module function structure (based on decompiled screen-vivante-omap5430.so)
 * Structure layout: 12-byte padding + 9 function pointers at specific offsets
 */
typedef struct {
    char padding[12];      /* Offsets 0x0 to 0xb - required by Screen API */
    
    /* Function pointers at specific offsets (discovered from decompiled source) */
    void* clear_fill;      /* offset 0xc  - Clear/fill operations */
    void* copy_blit;       /* offset 0x10 - Copy/blit operations */  
    void* stretch_blit;    /* offset 0x14 - Stretch operations */
    void* filter_blit;     /* offset 0x18 - Filter operations */
    void* alpha_blend;     /* offset 0x1c - Alpha blend operations */
    void* setup_target;    /* offset 0x20 - Setup/target operations */
    void* setup_source;    /* offset 0x24 - Source operations */
    void* flush_commit;    /* offset 0x28 - Flush/commit operations */
    void* cleanup;         /* offset 0x2c - Cleanup operations */
} evo_screen_blit_funcs_t;

/**
 * Screen blit module function pointer type
 */
typedef void (*evo_win_blit_module_getfuncs_t)(evo_screen_blit_funcs_t* funcs);

/**
 * Screen blit backend type for hardware acceleration
 */
typedef enum {
    EVO_SCREEN_BLIT_VIVANTE = 0,    /* Vivante GC320 backend */
    EVO_SCREEN_BLIT_POWERVR = 1,    /* PowerVR SGX540 backend */
    EVO_SCREEN_BLIT_GLES2 = 2,      /* OpenGL ES 2.0 backend */
    EVO_SCREEN_BLIT_AUTO = 3        /* Auto-select best available */
} evo_screen_blit_backend_t;

/**
 * Screen blit module context for hardware acceleration
 */
typedef struct {
    void* library_handle;                           /* dlopen handle for screen backend library */
    evo_win_blit_module_getfuncs_t getfuncs;       /* win_blit_module_getfuncs function pointer */
    evo_screen_blit_funcs_t blit_funcs;            /* Populated function pointers */
    evo_screen_blit_backend_t backend_type;        /* Active backend type */
    int is_initialized;                             /* Initialization state */
    char library_path[256];                         /* Loaded library path */
    char error_message[256];                        /* Last error message */
} evo_screen_blit_context_t;

/* ============================================================================
 * GAL HARDWARE ACCELERATION STRUCTURES
 * ============================================================================ */

/**
 * GAL function pointer types (using archive-discovered correct signatures)
 */
typedef int (*gcoOS_Construct_t)(void* os, void** os_handle);
typedef int (*gco2D_Construct_t)(void* os_handle, void** engine_2d);
typedef int (*gco2D_SetGenericTarget_t)(void* engine_2d, unsigned int* addresses, unsigned int* strides, unsigned int alignment, unsigned int format, unsigned int rotation, unsigned int width, unsigned int height);
typedef int (*gco2D_Clear_t)(void* hardware, unsigned int rect_count, void* rect, unsigned int color32, unsigned int color_convert);
typedef int (*gcoOS_Destruct_t)(void* os_handle);
typedef int (*gco2D_Destruct_t)(void* engine_2d);

/**
 * GAL rectangle structure for Clear operations
 */
typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} gcoBLIT_RECT;

/**
 * GAL hardware acceleration context
 */
typedef struct {
    void* gal_library;                              /* dlopen handle for GAL library */
    void* os_handle;                                /* GAL OS context handle */
    void* engine_2d;                                /* GAL 2D engine handle */
    
    /* Function pointers (archive-based correct signatures) */
    gcoOS_Construct_t gcoOS_Construct;
    gco2D_Construct_t gco2D_Construct;
    gco2D_SetGenericTarget_t gco2D_SetGenericTarget;
    gco2D_Clear_t gco2D_Clear;
    gcoOS_Destruct_t gcoOS_Destruct;
    gco2D_Destruct_t gco2D_Destruct;
    
    int is_initialized;                             /* Initialization state */
    char error_message[256];                        /* Last error message */
} evo_gal_context_t;

/* ============================================================================
 * BACKEND CONFIGURATION STRUCTURES
 * ============================================================================ */

/**
 * Backend configuration information
 */
typedef struct {
    evo_backend_type_t type;
    const char *name;
    const char *library_path;
    evo_buffer_strategy_t buffer_strategy;
    int is_hardware_accelerated;
    int is_recommended;
    const char *use_case;
    const char *requirements;
} evo_backend_config_t;

/**
 * Backend validation result
 */
typedef struct {
    int library_exists;
    int library_loadable;
    int has_screen_symbols;
    char error_message[256];
} evo_backend_test_result_t;

/**
 * Backend selection result
 */
typedef struct {
    evo_backend_type_t selected_backend;
    const evo_backend_config_t *config;
    evo_buffer_strategy_t buffer_strategy;
    char selection_reason[256];
} evo_backend_result_t;

/**
 * Backend selection request
 */
typedef struct {
    evo_backend_type_t backend_type;    /* Preferred backend (use EVO_BACKEND_AUTO for auto-select) */
    int force_backend;                  /* 1 = fail hard if backend unavailable, 0 = allow fallback */
    int verbose_logging;                /* 1 = show detailed selection process */
    int validate_libraries;             /* 1 = validate backend libraries before use */
} evo_backend_request_t;

/* ============================================================================
 * BACKEND INFORMATION
 * ============================================================================ */

/**
 * Get backend configuration by type
 * 
 * @param type Backend type
 * @return Backend configuration or NULL if invalid
 */
const evo_backend_config_t* evo_backend_get_config(evo_backend_type_t type);

/**
 * Get backend name string
 * 
 * @param type Backend type
 * @return Backend name string
 */
const char* evo_backend_get_name(evo_backend_type_t type);

/**
 * Convert backend type to string
 * 
 * @param type Backend type
 * @return Backend type string
 */
const char* evo_backend_type_to_string(evo_backend_type_t type);

/**
 * List all available backends with their status
 */
void evo_backend_list_all(void);

/* ============================================================================
 * BACKEND TESTING AND VALIDATION
 * ============================================================================ */

/**
 * Test if a backend is available and functional
 * 
 * @param type Backend type to test
 * @return Test result structure
 */
evo_backend_test_result_t evo_backend_test(evo_backend_type_t type);

/**
 * Validate backend or terminate with clear error message
 * 
 * @param type Backend type to validate
 */
void evo_backend_validate_or_die(evo_backend_type_t type);

/* ============================================================================
 * BACKEND SELECTION
 * ============================================================================ */

/**
 * Select optimal backend based on request
 * 
 * @param request Backend selection criteria
 * @return Selection result with chosen backend and reasoning
 */
evo_backend_result_t evo_backend_select(const evo_backend_request_t *request);

/**
 * Auto-select best available backend
 * 
 * @return Selected backend type or EVO_BACKEND_SOFTWARE if none available
 */
evo_backend_type_t evo_backend_auto_select(void);

/**
 * Create default backend request for auto-selection
 * 
 * @return Default request structure
 */
evo_backend_request_t evo_backend_create_auto_request(void);

/**
 * Create backend request for specific backend
 * 
 * @param type Backend type to request
 * @param force_backend 1 = fail hard if unavailable, 0 = allow fallback
 * @return Backend request structure
 */
evo_backend_request_t evo_backend_create_request(evo_backend_type_t type, int force_backend);

/* ============================================================================
 * SCREEN BLIT MODULE FUNCTIONS
 * ============================================================================ */

/**
 * Initialize Screen blit module for hardware acceleration
 * 
 * @param ctx Screen blit context to initialize
 * @return 0 on success, negative on error
 */
int evo_screen_blit_init(evo_screen_blit_context_t* ctx);

/**
 * Initialize Screen blit module with specific backend
 * 
 * @param ctx Screen blit context to initialize
 * @param backend_type Backend type to use
 * @return 0 on success, negative on error
 */
int evo_screen_blit_init_backend(evo_screen_blit_context_t* ctx, evo_screen_blit_backend_t backend_type);

/**
 * Auto-select and initialize best available Screen blit backend
 * 
 * @param ctx Screen blit context to initialize
 * @return 0 on success, negative on error
 */
int evo_screen_blit_init_auto(evo_screen_blit_context_t* ctx);

/**
 * Shutdown Screen blit module
 * 
 * @param ctx Screen blit context to shutdown
 */
void evo_screen_blit_shutdown(evo_screen_blit_context_t* ctx);

/**
 * Check if Screen blit module is available and initialized
 * 
 * @param ctx Screen blit context to check
 * @return 1 if available and ready, 0 if not
 */
int evo_screen_blit_is_ready(const evo_screen_blit_context_t* ctx);

/**
 * Get last error message from Screen blit module
 * 
 * @param ctx Screen blit context
 * @return Error message string
 */
const char* evo_screen_blit_get_error(const evo_screen_blit_context_t* ctx);

/**
 * Hardware-accelerated rectangle clear using Screen blit module
 * 
 * @param ctx Screen blit context
 * @param framebuffer Target framebuffer
 * @param width Framebuffer width
 * @param height Framebuffer height  
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param w Rectangle width
 * @param h Rectangle height
 * @param color Clear color (ARGB32)
 * @return 0 on success, negative on error
 */
int evo_screen_blit_clear_rect(evo_screen_blit_context_t* ctx, 
                               uint32_t* framebuffer, uint32_t width, uint32_t height,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                               uint32_t color);

/* ============================================================================
 * GAL HARDWARE ACCELERATION FUNCTIONS
 * ============================================================================ */

/**
 * Initialize GAL hardware acceleration context
 * 
 * @param ctx GAL context to initialize
 * @return 0 on success, negative on error
 */
int evo_gal_init(evo_gal_context_t* ctx);

/**
 * Shutdown GAL hardware acceleration context
 * 
 * @param ctx GAL context to shutdown
 */
void evo_gal_shutdown(evo_gal_context_t* ctx);

/**
 * Check if GAL hardware acceleration is available and initialized
 * 
 * @param ctx GAL context to check
 * @return 1 if available and ready, 0 if not
 */
int evo_gal_is_ready(const evo_gal_context_t* ctx);

/**
 * Get last error message from GAL
 * 
 * @param ctx GAL context
 * @return Error message string
 */
const char* evo_gal_get_error(const evo_gal_context_t* ctx);

/**
 * Hardware-accelerated rectangle clear using GAL
 * 
 * @param ctx GAL context
 * @param framebuffer Target framebuffer  
 * @param width Framebuffer width
 * @param height Framebuffer height
 * @param x Rectangle X position
 * @param y Rectangle Y position
 * @param w Rectangle width
 * @param h Rectangle height
 * @param color Clear color (ARGB32)
 * @return 0 on success, negative on error
 */
int evo_gal_clear_rect(evo_gal_context_t* ctx,
                       uint32_t* framebuffer, uint32_t width, uint32_t height,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color);

/**
 * Hardware-accelerated rectangle copy using Screen blit module
 * 
 * @param ctx Screen blit context
 * @param src_buffer Source buffer
 * @param src_width Source buffer width
 * @param src_height Source buffer height
 * @param dst_buffer Destination buffer
 * @param dst_width Destination buffer width
 * @param dst_height Destination buffer height
 * @param src_x Source X position
 * @param src_y Source Y position
 * @param dst_x Destination X position
 * @param dst_y Destination Y position
 * @param width Copy width
 * @param height Copy height
 * @return 0 on success, negative on error
 */
int evo_screen_blit_copy_rect(evo_screen_blit_context_t* ctx,
                              uint32_t* src_buffer, uint32_t src_width, uint32_t src_height,
                              uint32_t* dst_buffer, uint32_t dst_width, uint32_t dst_height,
                              uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
                              uint32_t width, uint32_t height);

/**
 * Hardware-accelerated flush/commit using Screen blit module
 * 
 * @param ctx Screen blit context
 * @return 0 on success, negative on error
 */
int evo_screen_blit_flush(evo_screen_blit_context_t* ctx);

/**
 * Comprehensive hardware validation for production deployment
 * 
 * @param ctx Screen blit context
 * @return 0 if hardware acceleration is ready, negative if insufficient
 */
int evo_screen_blit_validate_hardware_support(evo_screen_blit_context_t* ctx);

/**
 * Test Screen blit module performance
 * 
 * @param ctx Screen blit context
 * @param framebuffer Test framebuffer
 * @param width Framebuffer width
 * @param height Framebuffer height
 * @return Number of successful operations, negative on error
 */
int evo_screen_blit_performance_test(evo_screen_blit_context_t* ctx, 
                                     uint32_t* framebuffer, uint32_t width, uint32_t height);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Initialize backend system
 * 
 * @return 0 on success, negative on error
 */
int evo_backend_init(void);

/**
 * Shutdown backend system
 */
void evo_backend_shutdown(void);

/**
 * Check if backend system is initialized
 * 
 * @return 1 if initialized, 0 if not
 */
int evo_backend_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_BACKEND_H */

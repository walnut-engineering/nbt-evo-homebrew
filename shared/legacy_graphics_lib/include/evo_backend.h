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

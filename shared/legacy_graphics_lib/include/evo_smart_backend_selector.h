/*
 * EVO Smart Backend Selector - Fail-Hard Configuration System
 * 
 * This module provides intelligent backend selection with strict configuration
 * requirements. No silent fallbacks - every misconfiguration results in clear
 * failure messages to prevent masking setup issues.
 * 
 * Key Principles:
 * - FAIL HARD: No silent fallbacks that hide configuration problems
 * - BACKEND-SPECIFIC BUFFERING: Hardware backends use double buffering, Basic uses single
 * - EXPLICIT CONFIGURATION: Every backend choice must be intentional
 * - CLEAR ERROR MESSAGES: Immediate feedback on misconfigurations
 */

#ifndef EVO_SMART_BACKEND_SELECTOR_H
#define EVO_SMART_BACKEND_SELECTOR_H

#include <screen/screen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/* ============================================================================
 * BACKEND TYPES AND CAPABILITIES
 * ============================================================================ */

typedef enum {
    EVO_BACKEND_POWERVR_2D = 0,    /* Hardware-accelerated 2D (recommended) */
    EVO_BACKEND_GLES2,             /* OpenGL ES 2.0 hardware acceleration */
    EVO_BACKEND_GLES1,             /* OpenGL ES 1.0 legacy hardware */
    EVO_BACKEND_VIVANTE,           /* Vivante GPU for OMAP5430 */
    EVO_BACKEND_BASIC,             /* Basic libscreen.so.1 (single buffer only) */
    EVO_BACKEND_SOFTWARE,          /* Software rendering (usually broken) */
    EVO_BACKEND_COUNT
} evo_smart_backend_type_t;

typedef enum {
    EVO_BUFFER_SINGLE = 1,         /* Single buffering (Basic backend) */
    EVO_BUFFER_DOUBLE = 2          /* Double buffering (Hardware backends) */
} evo_buffer_strategy_t;

typedef struct {
    evo_smart_backend_type_t type;
    const char *name;
    const char *library_path;
    evo_buffer_strategy_t buffer_strategy;
    int is_hardware_accelerated;
    int is_recommended;
    const char *use_case;
    const char *requirements;
} evo_backend_config_t;

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
        .type = EVO_BACKEND_GLES1,
        .name = "OpenGL ES 1.0",
        .library_path = "/lib/screen-gles1.so",
        .buffer_strategy = EVO_BUFFER_DOUBLE,
        .is_hardware_accelerated = 1,
        .is_recommended = 0,
        .use_case = "Legacy 3D applications",
        .requirements = "OpenGL ES 1.0 compatible GPU"
    },
    {
        .type = EVO_BACKEND_VIVANTE,
        .name = "Vivante OMAP5430",
        .library_path = "/lib/screen-vivante-omap5430.so",
        .buffer_strategy = EVO_BUFFER_DOUBLE,
        .is_hardware_accelerated = 1,
        .is_recommended = 0,
        .use_case = "Platform-specific 2D acceleration",
        .requirements = "Vivante GPU, OMAP5430 platform"
    },
    {
        .type = EVO_BACKEND_BASIC,
        .name = "Basic Screen",
        .library_path = "/lib/libscreen.so.1",
        .buffer_strategy = EVO_BUFFER_SINGLE,   /* CRITICAL: Single buffer only! */
        .is_hardware_accelerated = 0,
        .is_recommended = 0,
        .use_case = "Fallback software rendering (single buffer only)",
        .requirements = "None - works with double buffering disabled"
    },
    {
        .type = EVO_BACKEND_SOFTWARE,
        .name = "Software",
        .library_path = "/lib/screen-sw.so",
        .buffer_strategy = EVO_BUFFER_SINGLE,
        .is_hardware_accelerated = 0,
        .is_recommended = 0,
        .use_case = "Pure software rendering (often broken)",
        .requirements = "None - CPU-based rendering"
    }
};

/* ============================================================================
 * BACKEND VALIDATION AND TESTING
 * ============================================================================ */

typedef struct {
    int library_exists;
    int library_loadable;
    int has_screen_symbols;
    char error_message[256];
} evo_backend_test_result_t;

/**
 * Test if a specific backend is available and functional
 * FAIL HARD: Returns detailed error information if backend is not usable
 */
evo_backend_test_result_t evo_smart_backend_test(evo_smart_backend_type_t backend_type);

/**
 * Validate backend configuration before use
 * FAIL HARD: Exits immediately if configuration is invalid
 */
void evo_smart_backend_validate_or_die(evo_smart_backend_type_t backend_type);

/* ============================================================================
 * SMART BACKEND SELECTION
 * ============================================================================ */

typedef struct {
    evo_smart_backend_type_t backend_type;
    int force_backend;              /* 1 = fail if backend unavailable, 0 = auto-select */
    int verbose_logging;            /* 1 = detailed backend selection logging */
    int validate_libraries;         /* 1 = test backend libraries before use */
} evo_smart_backend_request_t;

typedef struct {
    evo_smart_backend_type_t selected_backend;
    evo_buffer_strategy_t buffer_strategy;
    const evo_backend_config_t *config;
    char selection_reason[256];
} evo_smart_backend_result_t;

/**
 * Select backend with intelligent buffering strategy
 * FAIL HARD: Dies immediately if forced backend is unavailable
 * NO FALLBACKS: Every selection must be intentional
 */
evo_smart_backend_result_t evo_smart_backend_select(const evo_smart_backend_request_t *request);

/* ============================================================================
 * CONTEXT CREATION WITH BACKEND INTELLIGENCE
 * ============================================================================ */

typedef struct {
    screen_context_t screen_ctx;
    screen_window_t screen_win;
    screen_buffer_t *buffers;
    int buffer_count;
    int current_buffer;
    evo_smart_backend_result_t backend_info;
    int window_width;
    int window_height;
} evo_smart_display_context_t;

/**
 * Create display context with smart backend selection
 * FAIL HARD: Dies immediately on any configuration error
 * NO SILENT FIXES: Every problem must be explicitly resolved
 */
evo_smart_display_context_t* evo_smart_display_create(
    int width, int height, int x, int y,
    const evo_smart_backend_request_t *backend_request
);

/**
 * Create window buffers with backend-appropriate strategy
 * FAIL HARD: Dies if buffer creation fails
 */
int evo_smart_display_create_buffers(evo_smart_display_context_t *ctx);

/**
 * Present buffer using backend-appropriate method
 * Hardware backends: Uses double buffering
 * Basic backend: Uses single buffering
 */
int evo_smart_display_present(evo_smart_display_context_t *ctx);

/**
 * Get current drawing buffer
 * Returns appropriate buffer based on backend strategy
 */
screen_buffer_t evo_smart_display_get_buffer(evo_smart_display_context_t *ctx);

/**
 * Clean up display context
 */
void evo_smart_display_destroy(evo_smart_display_context_t *ctx);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Get human-readable backend information
 */
const evo_backend_config_t* evo_smart_backend_get_config(evo_smart_backend_type_t type);

/**
 * List all available backends with their status
 */
void evo_smart_backend_list_all(void);

/**
 * Convert backend type to string
 */
const char* evo_smart_backend_type_to_string(evo_smart_backend_type_t type);

/**
 * Convert string to backend type
 * FAIL HARD: Dies on invalid backend names
 */
evo_smart_backend_type_t evo_smart_backend_string_to_type(const char *name);

/* ============================================================================
 * ERROR HANDLING MACROS
 * ============================================================================ */

#define EVO_BACKEND_DIE(fmt, ...) do { \
    printf("💥 BACKEND CONFIGURATION ERROR: " fmt "\n", ##__VA_ARGS__); \
    printf("💥 FIX YOUR CONFIGURATION - NO FALLBACKS PROVIDED!\n"); \
    exit(1); \
} while(0)

#define EVO_BACKEND_WARN(fmt, ...) do { \
    printf("⚠️  BACKEND WARNING: " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define EVO_BACKEND_INFO(fmt, ...) do { \
    printf("ℹ️  BACKEND INFO: " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define EVO_BACKEND_SUCCESS(fmt, ...) do { \
    printf("✅ BACKEND SUCCESS: " fmt "\n", ##__VA_ARGS__); \
} while(0)

#endif /* EVO_SMART_BACKEND_SELECTOR_H */

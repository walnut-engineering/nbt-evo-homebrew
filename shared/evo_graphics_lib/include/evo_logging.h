/**
 * @file evo_logging.h
 * @brief EVO Graphics Library - Unified Logging System
 * 
 * Provides standardized logging levels and clean, production-ready output
 * without development phase references or excessive verbosity.
 * 
 * @version 2.1.0
 * @date July 27, 2025
 */

#ifndef EVO_LOGGING_H
#define EVO_LOGGING_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * LOG LEVELS
 * ============================================================================ */

typedef enum {
    EVO_LOG_SILENT = 0,    /* No output */
    EVO_LOG_ERROR  = 1,    /* Errors only */
    EVO_LOG_WARN   = 2,    /* Warnings and errors */
    EVO_LOG_INFO   = 3,    /* Informational messages */
    EVO_LOG_DEBUG  = 4     /* Detailed debug information */
} evo_log_level_t;

/* ============================================================================
 * LOGGING CONFIGURATION
 * ============================================================================ */

/**
 * Set the global log level for the EVO Graphics Library
 * Default: EVO_LOG_WARN (warnings and errors only, reduces console spam)
 */
void evo_log_set_level(evo_log_level_t level);

/**
 * Get the current log level
 */
evo_log_level_t evo_log_get_level(void);

/* ============================================================================
 * LOGGING MACROS
 * ============================================================================ */

#define EVO_LOG_ERROR(...) evo_log_internal(EVO_LOG_ERROR, "ERROR", __VA_ARGS__)
#define EVO_LOG_WARN(...)  evo_log_internal(EVO_LOG_WARN,  "WARN",  __VA_ARGS__)
#define EVO_LOG_INFO(...)  evo_log_internal(EVO_LOG_INFO,  "INFO",  __VA_ARGS__)
#define EVO_LOG_DEBUG(...) evo_log_internal(EVO_LOG_DEBUG, "DEBUG", __VA_ARGS__)

/**
 * Internal logging function - do not call directly
 */
void evo_log_internal(evo_log_level_t level, const char* level_str, const char* format, ...);

/* ============================================================================
 * CONVENIENCE MACROS
 * ============================================================================ */

/* Context-related logging */
#define EVO_LOG_CONTEXT_CREATED(w, h, x, y) \
    EVO_LOG_INFO("Context created: %dx%d at (%d,%d)", w, h, x, y)

#define EVO_LOG_CONTEXT_DESTROYED() \
    EVO_LOG_INFO("Context destroyed")

/* Backend-related logging */
#define EVO_LOG_BACKEND_SELECTED(name) \
    EVO_LOG_INFO("Backend selected: %s", name)

#define EVO_LOG_HARDWARE_ACCEL(enabled) \
    EVO_LOG_INFO("Hardware acceleration: %s", (enabled) ? "enabled" : "disabled")

/* Display-related logging */
#define EVO_LOG_DISPLAY_SHOWN() \
    EVO_LOG_DEBUG("Display shown")

#define EVO_LOG_DISPLAY_UPDATED() \
    EVO_LOG_DEBUG("Display updated")

#define EVO_LOG_BUFFERS_SWAPPED() \
    EVO_LOG_DEBUG("Buffers swapped")

/* Error conditions */
#define EVO_LOG_INIT_FAILED(component) \
    EVO_LOG_ERROR("Failed to initialize %s", component)

#define EVO_LOG_ALLOCATION_FAILED(what) \
    EVO_LOG_ERROR("Memory allocation failed: %s", what)

#define EVO_LOG_INVALID_PARAM(param) \
    EVO_LOG_ERROR("Invalid parameter: %s", param)

#ifdef __cplusplus
}
#endif

#endif /* EVO_LOGGING_H */

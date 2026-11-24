/**
 * @file evo_logging.c
 * @brief EVO Graphics Library - Unified Logging Implementation
 * 
 * Implements standardized logging with configurable levels and clean output.
 * 
 * @version 2.1.0
 * @date July 27, 2025
 */

#include "../include/evo_logging.h"
#include <stdarg.h>
#include <stdio.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static evo_log_level_t g_log_level = EVO_LOG_WARN;  /* Default: warnings and errors only */

/* ============================================================================
 * PUBLIC FUNCTIONS
 * ============================================================================ */

void evo_log_set_level(evo_log_level_t level) {
    g_log_level = level;
}

evo_log_level_t evo_log_get_level(void) {
    return g_log_level;
}

void evo_log_internal(evo_log_level_t level, const char* level_str, const char* format, ...) {
    /* Check if this message should be logged */
    if (level > g_log_level) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    /* Output format: [EVO:LEVEL] message */
    printf("[EVO:%s] ", level_str);
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
    
    va_end(args);
}

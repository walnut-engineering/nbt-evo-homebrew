/*
 * QNX Application Framework - Common Utilities for EVO Graphics Applications
 * 
 * This framework provides common functionality for QNX graphics applications including:
 * - Standardized initialization and cleanup patterns
 * - Signal handling and graceful shutdown
 * - Screenshot marker utilities  
 * - Error handling and logging
 * - Command-line argument processing
 * - Phase-based execution patterns
 * 
 * Designed for reuse across different QNX graphics applications, not just examples.
 */

#ifndef EVO_UTIL_H
#define EVO_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "evo_display_helper.h"
#include "evo_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common configuration structure for examples */
typedef struct {
    int width;
    int height;
    int x_offset;
    int y_offset;
    uint32_t surface_id;
    uint32_t layer_id;
    const char *title;
    int enable_signal_handling;
} evo_example_config_t;

/* Example execution mode */
typedef enum {
    EVO_EXAMPLE_MODE_INTERACTIVE,
    EVO_EXAMPLE_MODE_AUTO,
    EVO_EXAMPLE_MODE_HELP
} evo_example_mode_t;

/* Phase execution configuration */
typedef struct {
    const char *name;
    int duration_seconds;
    const char *screenshot_suffix;
    void (*phase_func)(void *context);
} evo_example_phase_t;

/* Global state for signal handling */
extern volatile int g_example_running;

/* Standard signal handler */
void evo_example_signal_handler(int sig);

/* Setup signal handling for examples */
void evo_example_setup_signals(void);

/* Screenshot marker utilities */
void evo_example_screenshot(const char *binary_name, const char *phase_name);
void evo_example_screenshot_numbered(const char *binary_name, int phase_number);
void evo_example_screenshot_with_wait(const char *binary_name, const char *phase_name);

/* Command line argument handling */
evo_example_mode_t evo_example_parse_args(int argc, char *argv[], const char *program_name);
void evo_example_show_help(const char *program_name, const char *description, 
                          const char *features[], int feature_count);

/* Phase execution utilities */
int evo_example_run_phases(const char *binary_name, evo_example_phase_t phases[], 
                          int phase_count, void *context);
void evo_example_execute_phase(const char *binary_name, const evo_example_phase_t *phase, void *context);

/* Standardized cleanup and exit */
void evo_example_cleanup_and_exit(evo_display_context_t **display, int exit_code);

/* Standard example initialization */
evo_display_context_t* evo_example_init_display(const evo_example_config_t *config);

/* Standard example cleanup */
void evo_example_cleanup(evo_display_context_t **display, evo_graphics_context_t **graphics);

/* Error handling macros */
#define EVO_EXAMPLE_ERROR(msg) do { \
    fprintf(stderr, "❌ ERROR: %s (file: %s, line: %d)\n", msg, __FILE__, __LINE__); \
    exit(1); \
} while(0)

#define EVO_EXAMPLE_WARNING(msg) do { \
    fprintf(stderr, "⚠️  WARNING: %s (file: %s, line: %d)\n", msg, __FILE__, __LINE__); \
} while(0)

#define EVO_EXAMPLE_INFO(msg) do { \
    printf("ℹ️  INFO: %s\n", msg); \
    fflush(stdout); \
} while(0)

#define EVO_EXAMPLE_SUCCESS(msg) do { \
    printf("✅ SUCCESS: %s\n", msg); \
    fflush(stdout); \
} while(0)

/* Phase timing utilities */
void evo_example_sleep_phase(int seconds);
void evo_example_wait_for_user(const char *prompt);

/* Standard example loop utilities */
typedef struct {
    int max_iterations;
    int current_iteration;
    float delay_seconds;
    int (*should_continue)(void);
} evo_example_loop_t;

int evo_example_run_loop(evo_example_loop_t *loop, void (*update_func)(int iteration));

#ifdef __cplusplus
}
#endif

#endif /* EVO_UTIL_H */

/*
 * QNX Example Utilities - Implementation
 * 
 * Common utilities to eliminate code duplication across QNX graphics examples.
 */

#include "evo_util.h"

/* Global state for signal handling */
volatile int g_example_running = 1;

/* Standard signal handler */
void evo_example_signal_handler(int sig) {
    printf("\n🛑 Signal %d received, initiating graceful shutdown...\n", sig);
    g_example_running = 0;
}

/* Setup signal handling for examples */
void evo_example_setup_signals(void) {
    signal(SIGINT, evo_example_signal_handler);
    signal(SIGTERM, evo_example_signal_handler);
    printf("✅ Signal handlers configured (SIGINT, SIGTERM)\n");
}

/* Screenshot marker utilities */
void evo_example_screenshot(const char *binary_name, const char *phase_name) {
    if (!binary_name || !phase_name) {
        EVO_EXAMPLE_WARNING("Invalid screenshot parameters");
        return;
    }
    
    printf("   READY_FOR_SCREENSHOT: %s_%s.bmp\n", binary_name, phase_name);
    fflush(stdout);
}

void evo_example_screenshot_numbered(const char *binary_name, int phase_number) {
    if (!binary_name || phase_number < 1) {
        EVO_EXAMPLE_WARNING("Invalid screenshot parameters");
        return;
    }
    
    printf("   READY_FOR_SCREENSHOT: %s_phase%d.bmp\n", binary_name, phase_number);
    fflush(stdout);
}

/* Standard example initialization */
evo_display_context_t* evo_example_init_display(const evo_example_config_t *config) {
    if (!config) {
        EVO_EXAMPLE_ERROR("Configuration is required");
        return NULL;
    }
    
    EVO_EXAMPLE_INFO("Initializing QNX display system...");
    
    if (config->title) {
        printf("📺 Creating display: %s (%dx%d at %d,%d)\n", 
               config->title, config->width, config->height, 
               config->x_offset, config->y_offset);
    }
    
    evo_display_context_t *display = evo_display_create(
        config->width, config->height, 
        config->x_offset, config->y_offset,
        config->surface_id, config->layer_id
    );
    
    if (!display) {
        EVO_EXAMPLE_ERROR("Failed to create display context");
        return NULL;
    }
    
    EVO_EXAMPLE_SUCCESS("Display context created successfully");
    
    /* Setup signal handling if requested */
    if (config->enable_signal_handling) {
        evo_example_setup_signals();
    }
    
    return display;
}

/* Standard example cleanup */
void evo_example_cleanup(evo_display_context_t **display, evo_graphics_context_t **graphics) {
    EVO_EXAMPLE_INFO("Starting cleanup process...");
    
    if (graphics && *graphics) {
        evo_graphics_destroy_context(graphics);
        EVO_EXAMPLE_SUCCESS("Graphics context destroyed");
    }
    
    if (display && *display) {
        evo_display_destroy(display);
        EVO_EXAMPLE_SUCCESS("Display context destroyed");
    }
    
    EVO_EXAMPLE_SUCCESS("Cleanup completed successfully");
}

/* Phase timing utilities */
void evo_example_sleep_phase(int seconds) {
    if (seconds <= 0) return;
    
    printf("⏱️  Phase delay: %d seconds...\n", seconds);
    fflush(stdout);
    sleep(seconds);
}

void evo_example_wait_for_user(const char *prompt) {
    printf("⏸️  %s (Press Enter to continue...)\n", 
           prompt ? prompt : "Waiting for user input");
    fflush(stdout);
    getchar();
}

/* Standard example loop utilities */
int evo_example_run_loop(evo_example_loop_t *loop, void (*update_func)(int iteration)) {
    if (!loop || !update_func) {
        EVO_EXAMPLE_ERROR("Invalid loop parameters");
        return -1;
    }
    
    EVO_EXAMPLE_INFO("Starting example loop...");
    printf("   Max iterations: %d\n", loop->max_iterations);
    printf("   Delay per iteration: %.2f seconds\n", loop->delay_seconds);
    
    for (loop->current_iteration = 0; 
         loop->current_iteration < loop->max_iterations && g_example_running; 
         loop->current_iteration++) {
        
        /* Check custom continuation condition */
        if (loop->should_continue && !loop->should_continue()) {
            EVO_EXAMPLE_INFO("Loop terminated by custom condition");
            break;
        }
        
        /* Execute update function */
        printf("🔄 Loop iteration %d/%d\n", 
               loop->current_iteration + 1, loop->max_iterations);
        update_func(loop->current_iteration);
        
        /* Inter-iteration delay */
        if (loop->delay_seconds > 0) {
            usleep((int)(loop->delay_seconds * 1000000));
        }
    }
    
    EVO_EXAMPLE_SUCCESS("Example loop completed");
    printf("   Total iterations: %d\n", loop->current_iteration);
    
    return loop->current_iteration;
}

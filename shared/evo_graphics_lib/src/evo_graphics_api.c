/*
 * EVO Graphics Library v2.0 - Unified API Implementation
 * Consolidates main API and wrapper functions into single file
 * Phase 2 Consolidation Result
 */

#include "evo_graphics_lib.h"  /* Main header with version constants */
#include "evo_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static int evo_initialized = 0;

/* ============================================================================
 * CORE INITIALIZATION & SHUTDOWN
 * ============================================================================ */

int evo_graphics_init(void) {
    if (evo_initialized) {
        printf("[EVO] Already initialized\n");
        return 0;
    }

    printf("[EVO] Initializing graphics subsystems...\n");

    // Initialize Backend subsystem
    if (evo_backend_init() != 0) {
        fprintf(stderr, "[EVO] ERROR: Backend initialization failed\n");
        return -1;
    }

    // Initialize Text subsystem
    if (evo_text_init() != 0) {
        fprintf(stderr, "[EVO] ERROR: Text initialization failed\n");
        evo_backend_shutdown();
        return -1;
    }

    evo_initialized = 1;
    printf("[EVO] Graphics initialization complete\n");
    return 0;
}

void evo_graphics_shutdown(void) {
    if (!evo_initialized) {
        printf("[EVO] Already shutdown or never initialized\n");
        return;
    }

    printf("[EVO] Shutting down graphics subsystems...\n");

    // Cleanup in reverse order
    evo_text_shutdown();
    evo_backend_shutdown();

    evo_initialized = 0;
    printf("[EVO] Graphics shutdown complete\n");
}

int evo_graphics_is_initialized(void) {
    return evo_initialized;
}

const char* evo_graphics_get_version(void) {
    return EVO_GRAPHICS_VERSION;
}

/* ============================================================================
 * CONTEXT MANAGEMENT
 * ============================================================================ */

evo_context_t* evo_graphics_create_context(int width, int height) {
    if (!evo_initialized) {
        fprintf(stderr, "[EVO] ERROR: Graphics not initialized\n");
        return NULL;
    }

    return evo_context_create(width, height, 0, 0);
}

void evo_graphics_destroy_context(evo_context_t* ctx) {
    if (ctx) {
        evo_context_destroy(ctx);
    }
}

int evo_graphics_present(evo_context_t* ctx) {
    if (!ctx) {
        fprintf(stderr, "[EVO] ERROR: Invalid context\n");
        return -1;
    }

    return evo_context_present(ctx);
}

/* ============================================================================
 * SIMPLIFIED WRAPPER API
 * These functions provide a simpler interface by wrapping the main API calls
 * ============================================================================ */

int evo_init(void) {
    return evo_graphics_init();
}

void evo_shutdown(void) {
    evo_graphics_shutdown();
}

int evo_is_initialized(void) {
    return evo_graphics_is_initialized();
}

evo_context_t* evo_create_context(int width, int height) {
    return evo_graphics_create_context(width, height);
}

void evo_destroy_context(evo_context_t* ctx) {
    evo_graphics_destroy_context(ctx);
}

int evo_present(evo_context_t* ctx) {
    return evo_graphics_present(ctx);
}

/* ============================================================================
 * SIMPLIFIED DRAWING API
 * ============================================================================ */

/* Note: Drawing functions (evo_clear, evo_draw_line, evo_draw_rect, evo_draw_circle)
 * are already implemented in core/evo_graphics_unified.c and don't need wrappers.
 */

/* ============================================================================
 * SIMPLIFIED TEXT API (FreeType Only)
 * ============================================================================ */

/* Note: evo_draw_text is implemented in text/evo_text_unified.c */

int evo_draw_text_with_font(evo_context_t* ctx, const char* text, int x, int y, 
                            const char* font_path, int size, uint32_t color) {
    return evo_text_draw_with_font(ctx, text, x, y, font_path, size, color);
}

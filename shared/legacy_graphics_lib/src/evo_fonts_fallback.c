/*
 * EVO Fonts - Simplified Implementation (Bitmap Fallback)
 * 
 * This is a simplified version that provides the font API but uses
 * bitmap font fallbacks instead of FreeType2 for compatibility.
 */

#include "evo_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global font manager instance */
static evo_font_manager_t g_font_manager = {0};
static int g_font_system_initialized = 0;

/* ============================================================================
 * SIMPLIFIED FONT MANAGER IMPLEMENTATION
 * ============================================================================ */

/**
 * Initialize font manager (simplified)
 */
int evo_font_manager_init(evo_font_manager_t* manager) {
    if (!manager) return -1;
    
    printf("📋 Initializing simplified font manager (bitmap fallback mode)\n");
    
    /* Clear the manager structure */
    memset(manager, 0, sizeof(evo_font_manager_t));
    
    /* Initialize database */
    manager->database.initialized = 1;
    manager->database.count = 0;
    
    /* Initialize loaded fonts array */
    manager->loaded_count = 0;
    
    printf("✅ Font manager initialized in fallback mode\n");
    return 0;
}

/**
 * Cleanup font manager
 */
void evo_font_manager_cleanup(evo_font_manager_t* manager) {
    if (!manager) return;
    
    printf("🧹 Cleaning up font manager...\n");
    
    /* Free loaded fonts */
    for (int i = 0; i < manager->loaded_count; i++) {
        if (manager->loaded_fonts[i]) {
            free(manager->loaded_fonts[i]);
            manager->loaded_fonts[i] = NULL;
        }
    }
    
    /* Clear everything */
    memset(manager, 0, sizeof(evo_font_manager_t));
    
    printf("✅ Font manager cleanup complete\n");
}

/* ============================================================================
 * FONT DISCOVERY (SIMPLIFIED)
 * ============================================================================ */

/**
 * Discover system fonts (simplified stub)
 */
int evo_font_discover_system_fonts(evo_font_manager_t* manager) {
    if (!manager) return -1;
    
    printf("📋 Font discovery: using bitmap fallback mode\n");
    
    /* Create a few dummy font entries for testing */
    if (manager->database.count < EVO_FONT_MAX_FONTS - 3) {
        /* Add some common font names that will fallback to bitmap */
        strcpy(manager->database.fonts[manager->database.count].family, "sans-serif");
        strcpy(manager->database.fonts[manager->database.count].style_name, "Regular");
        strcpy(manager->database.fonts[manager->database.count].path, "/usr/share/fonts/fallback");
        manager->database.fonts[manager->database.count].type = EVO_FONT_TYPE_SANS_SERIF;
        manager->database.fonts[manager->database.count].weight = EVO_FONT_WEIGHT_REGULAR;
        manager->database.fonts[manager->database.count].style = EVO_FONT_STYLE_NORMAL;
        manager->database.count++;
        
        strcpy(manager->database.fonts[manager->database.count].family, "serif");
        strcpy(manager->database.fonts[manager->database.count].style_name, "Regular");
        strcpy(manager->database.fonts[manager->database.count].path, "/usr/share/fonts/fallback");
        manager->database.fonts[manager->database.count].type = EVO_FONT_TYPE_SERIF;
        manager->database.fonts[manager->database.count].weight = EVO_FONT_WEIGHT_REGULAR;
        manager->database.fonts[manager->database.count].style = EVO_FONT_STYLE_NORMAL;
        manager->database.count++;
        
        strcpy(manager->database.fonts[manager->database.count].family, "monospace");
        strcpy(manager->database.fonts[manager->database.count].style_name, "Regular");
        strcpy(manager->database.fonts[manager->database.count].path, "/usr/share/fonts/fallback");
        manager->database.fonts[manager->database.count].type = EVO_FONT_TYPE_MONOSPACE;
        manager->database.fonts[manager->database.count].weight = EVO_FONT_WEIGHT_REGULAR;
        manager->database.fonts[manager->database.count].style = EVO_FONT_STYLE_NORMAL;
        manager->database.count++;
    }
    
    printf("✅ Added %d fallback font entries\n", manager->database.count);
    return manager->database.count;
}

/* ============================================================================
 * FONT SEARCH FUNCTIONS
 * ============================================================================ */

/**
 * Find font by family name
 */
const evo_font_info_t* evo_font_find_by_family(const evo_font_manager_t* manager, 
                                               const char* family_name) {
    if (!manager || !family_name) return NULL;
    
    for (int i = 0; i < manager->database.count; i++) {
        if (strcasecmp(manager->database.fonts[i].family, family_name) == 0) {
            return &manager->database.fonts[i];
        }
    }
    
    /* Return first available font as fallback */
    if (manager->database.count > 0) {
        printf("📋 Font '%s' not found, using fallback: '%s'\n", 
               family_name, manager->database.fonts[0].family);
        return &manager->database.fonts[0];
    }
    
    return NULL;
}

/**
 * Find font by type and weight
 */
const evo_font_info_t* evo_font_find_by_type(const evo_font_manager_t* manager,
                                             evo_font_type_t type,
                                             evo_font_weight_t weight) {
    if (!manager) return NULL;
    
    /* Find best match */
    const evo_font_info_t* best_match = NULL;
    int best_weight_diff = 1000;
    
    for (int i = 0; i < manager->database.count; i++) {
        const evo_font_info_t* font = &manager->database.fonts[i];
        if (font->type == type) {
            int weight_diff = abs(font->weight - weight);
            if (weight_diff < best_weight_diff) {
                best_weight_diff = weight_diff;
                best_match = font;
            }
        }
    }
    
    return best_match;
}

/* ============================================================================
 * FONT LOADING (SIMPLIFIED)
 * ============================================================================ */

/**
 * Load a font (simplified)
 */
evo_font_handle_t* evo_font_load(evo_font_manager_t* manager,
                                 const evo_font_info_t* font_info,
                                 int pixel_size) {
    if (!manager || !font_info || pixel_size <= 0) return NULL;
    
    printf("📋 Loading font '%s' at %dpx (fallback mode)\n", 
           font_info->family, pixel_size);
    
    /* Allocate font handle */
    evo_font_handle_t* handle = malloc(sizeof(evo_font_handle_t));
    if (!handle) return NULL;
    
    /* Initialize handle (no FreeType face) */
    handle->info = (evo_font_info_t*)font_info;
    handle->current_size = pixel_size;
    handle->ref_count = 1;
    handle->face = NULL;  /* No FreeType face in fallback mode */
    
    printf("✅ Font handle created for '%s'\n", font_info->family);
    return handle;
}

/**
 * Load a font (convenience wrapper)
 */
evo_font_handle_t* evo_font_load_font(evo_font_manager_t* manager,
                                      const evo_font_info_t* font_info,
                                      int pixel_size) {
    return evo_font_load(manager, font_info, pixel_size);
}

/* ============================================================================
 * FONT MEASUREMENT (SIMPLIFIED)
 * ============================================================================ */

/**
 * Measure text width (simplified)
 */
int evo_font_measure_text(evo_font_handle_t* handle, const char* text, int length) {
    if (!handle || !text) return -1;
    
    /* Use strlen if length is -1 */
    if (length < 0) {
        length = strlen(text);
    }
    
    /* Estimate 8 pixels per character (bitmap font width) */
    return length * 8;
}

/**
 * Get font height (simplified)
 */
int evo_font_get_height(evo_font_handle_t* handle) {
    if (!handle) return -1;
    
    /* Return typical bitmap font height */
    return 16;
}

/* ============================================================================
 * FONT CLEANUP
 * ============================================================================ */

/**
 * Release a font handle
 */
void evo_font_release(evo_font_handle_t* handle) {
    if (!handle) return;
    
    handle->ref_count--;
    if (handle->ref_count <= 0) {
        free(handle);
    }
}

/* ============================================================================
 * GLOBAL FONT MANAGER
 * ============================================================================ */

/**
 * Get global font manager
 */
evo_font_manager_t* evo_font_get_manager(void) {
    return &g_font_manager;
}

/**
 * Initialize global font system
 */
int evo_font_global_init(void) {
    if (g_font_system_initialized) {
        return 0;  /* Already initialized */
    }
    
    int result = evo_font_manager_init(&g_font_manager);
    if (result == 0) {
        /* Discover system fonts */
        evo_font_discover_system_fonts(&g_font_manager);
        g_font_system_initialized = 1;
    }
    
    return result;
}

/**
 * Cleanup global font system
 */
void evo_font_global_cleanup(void) {
    if (!g_font_system_initialized) return;
    
    evo_font_manager_cleanup(&g_font_manager);
    g_font_system_initialized = 0;
}

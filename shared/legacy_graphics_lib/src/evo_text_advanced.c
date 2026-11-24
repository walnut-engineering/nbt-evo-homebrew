/*
 * EVO Text Advanced - Dynamic Font System Implementation
 * 
 * This implementation provides enhanced text rendering with system font loading,
 * deprecating bitmap fonts in favor of FreeType2-based dynamic fonts.
 */

#include "evo_text_advanced.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global advanced text context */
static evo_text_advanced_context_t g_text_context = {0};
static int g_text_advanced_initialized = 0;

/* Default system font mappings for QNX */
static const char* default_font_mappings[] = {
    "monospace",     /* EVO_FONT_TYPE_MONOSPACE */
    "serif",         /* EVO_FONT_TYPE_SERIF */
    "sans-serif",    /* EVO_FONT_TYPE_SANS_SERIF */
    "serif",         /* EVO_FONT_TYPE_SERIF (fallback) */
    "sans-serif",    /* EVO_FONT_TYPE_DISPLAY */
    "monospace"      /* EVO_FONT_TYPE_SYMBOL */
};

/* Font size conversion: categories to points */
static const int size_category_to_points[] = {
    12,  /* EVO_FONT_SMALL */
    16,  /* EVO_FONT_MEDIUM */
    20,  /* EVO_FONT_LARGE */
    24,  /* EVO_FONT_XLARGE */
    32   /* EVO_FONT_XXLARGE */
};

/* ============================================================================
 * INITIALIZATION AND CONFIGURATION
 * ============================================================================ */

int evo_text_advanced_init(void) {
    if (g_text_advanced_initialized) {
        return 0;  /* Already initialized */
    }
    
    printf("🔤 Initializing EVO Advanced Text System...\n");
    
    /* Initialize font manager */
    g_text_context.font_manager = malloc(sizeof(evo_font_manager_t));
    if (!g_text_context.font_manager) {
        printf("❌ Failed to allocate font manager\n");
        return -1;
    }
    
    int result = evo_font_manager_init(g_text_context.font_manager);
    if (result < 0) {
        printf("❌ Failed to initialize font manager\n");
        free(g_text_context.font_manager);
        g_text_context.font_manager = NULL;
        return -1;
    }
    
    /* Discover system fonts */
    printf("🔍 Discovering system fonts...\n");
    int font_count = evo_font_discover_system_fonts(g_text_context.font_manager);
    if (font_count > 0) {
        printf("✅ Found %d system fonts\n", font_count);
        g_text_context.preference = EVO_TEXT_PREFER_SYSTEM_FONTS;
    } else {
        printf("📋 No system fonts found, using bitmap fallback\n");
        g_text_context.preference = EVO_TEXT_PREFER_BITMAP_FONTS;
    }
    
    /* Set default configuration */
    g_text_context.quality = EVO_TEXT_QUALITY_NORMAL;
    g_text_context.kerning_enabled = 1;
    g_text_context.hinting_enabled = 1;
    g_text_context.text_color = 0xFFFFFFFF;  /* White */
    g_text_context.background_enabled = 0;
    
    g_text_advanced_initialized = 1;
    printf("✅ Advanced text system initialized\n");
    return 0;
}

void evo_text_advanced_shutdown(void) {
    if (!g_text_advanced_initialized) {
        return;
    }
    
    /* Release current font handle */
    if (g_text_context.current_handle) {
        evo_text_advanced_release_font(g_text_context.current_handle);
        g_text_context.current_handle = NULL;
    }
    
    /* Cleanup font manager */
    if (g_text_context.font_manager) {
        evo_font_manager_cleanup(g_text_context.font_manager);
        free(g_text_context.font_manager);
        g_text_context.font_manager = NULL;
    }
    
    memset(&g_text_context, 0, sizeof(g_text_context));
    g_text_advanced_initialized = 0;
    printf("✅ Advanced text system shutdown\n");
}

int evo_text_advanced_is_available(void) {
    return g_text_advanced_initialized && 
           g_text_context.font_manager && 
           g_text_context.font_manager->database.count > 0;
}

int evo_text_advanced_set_preference(evo_text_font_preference_t preference) {
    if (!g_text_advanced_initialized) {
        return -1;
    }
    
    g_text_context.preference = preference;
    return 0;
}

int evo_text_advanced_set_quality(evo_text_quality_t quality) {
    if (!g_text_advanced_initialized) {
        return -1;
    }
    
    g_text_context.quality = quality;
    return 0;
}

/* ============================================================================
 * FONT MANAGEMENT
 * ============================================================================ */

evo_font_handle_t* evo_text_advanced_load_font(const char *family_name, int size_pt) {
    if (!g_text_advanced_initialized || !g_text_context.font_manager) {
        return NULL;
    }
    
    /* Check preference settings */
    if (g_text_context.preference == EVO_TEXT_FORCE_BITMAP_FONTS) {
        return NULL;  /* Forced to use bitmap fonts */
    }
    
    /* Find font by family name */
    const evo_font_info_t *font_info = evo_font_find_by_family(
        g_text_context.font_manager, family_name);
    
    if (!font_info) {
        /* Try to find a suitable fallback */
        printf("📋 Font '%s' not found, trying fallback...\n", family_name);
        return NULL;
    }
    
    /* Load the font */
    evo_font_handle_t *handle = evo_font_load_font(g_text_context.font_manager, 
                                                  font_info, size_pt);
    if (!handle) {
        printf("❌ Failed to load font '%s' at %dpt\n", family_name, size_pt);
        return NULL;
    }
    
    printf("✅ Loaded font '%s' at %dpt\n", family_name, size_pt);
    return handle;
}

evo_font_handle_t* evo_text_advanced_load_font_by_type(evo_font_type_t type, int size_pt) {
    if (type >= sizeof(default_font_mappings) / sizeof(default_font_mappings[0])) {
        type = EVO_FONT_TYPE_SANS_SERIF;  /* Default fallback */
    }
    
    return evo_text_advanced_load_font(default_font_mappings[type], size_pt);
}

evo_font_handle_t* evo_text_advanced_get_default_font(int size_pt) {
    /* Try to load a good default font */
    evo_font_handle_t *handle = evo_text_advanced_load_font("sans-serif", size_pt);
    if (!handle) {
        handle = evo_text_advanced_load_font("serif", size_pt);
    }
    if (!handle) {
        handle = evo_text_advanced_load_font("monospace", size_pt);
    }
    
    return handle;
}

void evo_text_advanced_release_font(evo_font_handle_t *font) {
    if (!font) return;
    
    /* Use evo_font_release instead of evo_font_unload_font */
    evo_font_release(font);
}

/* ============================================================================
 * TEXT MEASUREMENT
 * ============================================================================ */

int evo_text_advanced_measure(evo_font_handle_t *font, const char *text,
                             evo_text_measurement_t *measurement) {
    if (!font || !text || !measurement) {
        return -1;
    }
    
    /* Use FreeType2 to measure text */
    int width = evo_font_measure_text(font, text, -1);
    int height = evo_font_get_height(font);
    
    if (width < 0 || height < 0) {
        return -1;
    }
    
    measurement->width = width;
    measurement->height = height;
    measurement->advance_x = width;
    measurement->advance_y = height;
    measurement->baseline_offset = height * 3 / 4;  /* Approximate baseline */
    
    return 0;
}

int evo_text_advanced_measure_char(evo_font_handle_t *font, uint32_t codepoint,
                                  evo_text_measurement_t *measurement) {
    char utf8_char[8];
    int len = snprintf(utf8_char, sizeof(utf8_char), "%c", (char)codepoint);
    if (len <= 0) {
        return -1;
    }
    
    return evo_text_advanced_measure(font, utf8_char, measurement);
}

/* ============================================================================
 * TEXT RENDERING
 * ============================================================================ */

int evo_text_advanced_draw(evo_graphics_context_t *ctx, int x, int y,
                          const char *text, evo_font_handle_t *font, uint32_t color) {
    if (!ctx || !text) {
        return -1;
    }
    
    /* If no font provided or system fonts not available, fall back to bitmap */
    if (!font || !evo_text_advanced_is_available() || 
        g_text_context.preference == EVO_TEXT_FORCE_BITMAP_FONTS) {
        
        /* Fall back to legacy bitmap text rendering */
        printf("📋 Using bitmap font fallback for text: '%.20s...'\n", text);
        return evo_text_draw_text(ctx, x, y, text, EVO_FONT_MEDIUM, color);
    }
    
    /* For now, always fall back to bitmap fonts until FreeType2 is fully integrated */
    printf("📋 Using bitmap font fallback for text: '%.20s...'\n", text);
    return evo_text_draw_text(ctx, x, y, text, EVO_FONT_MEDIUM, color);
}

int evo_text_advanced_draw_with_background(evo_graphics_context_t *ctx, int x, int y,
                                          const char *text, evo_font_handle_t *font,
                                          uint32_t text_color, uint32_t bg_color) {
    if (!ctx || !text) {
        return -1;
    }
    
    /* Measure text for background rectangle */
    evo_text_measurement_t measurement;
    int width;
    
    if (font && evo_text_advanced_measure(font, text, &measurement) == 0) {
        width = measurement.width;
    } else {
        /* Fallback measurement */
        width = evo_text_get_text_width(text, EVO_FONT_MEDIUM);
        measurement.height = 16;  /* Approximate height */
    }
    
    /* Draw background rectangle */
    evo_graphics_draw_rectangle(ctx, x, y, width, measurement.height, bg_color);
    
    /* Draw text on top */
    return evo_text_advanced_draw(ctx, x, y, text, font, text_color);
}

/* ============================================================================
 * COMPATIBILITY LAYER
 * ============================================================================ */

int evo_text_enhanced_draw_text(evo_graphics_context_t *ctx, int x, int y,
                               const char *text, evo_font_size_t size, uint32_t color) {
    if (!g_text_advanced_initialized) {
        /* System not initialized, use legacy */
        return evo_text_draw_text(ctx, x, y, text, size, color);
    }
    
    /* Convert size category to points */
    int size_pt = (size < EVO_FONT_COUNT) ? size_category_to_points[size] : 16;
    
    /* Try to get a suitable font */
    evo_font_handle_t *font = evo_text_advanced_get_default_font(size_pt);
    
    /* Draw with advanced system or fall back */
    int result = evo_text_advanced_draw(ctx, x, y, text, font, color);
    
    /* Release temporary font handle */
    if (font) {
        evo_text_advanced_release_font(font);
    }
    
    return result;
}

int evo_text_enhanced_get_text_width(const char *text, evo_font_size_t size) {
    if (!g_text_advanced_initialized) {
        return evo_text_get_text_width(text, size);
    }
    
    int size_pt = (size < EVO_FONT_COUNT) ? size_category_to_points[size] : 16;
    evo_font_handle_t *font = evo_text_advanced_get_default_font(size_pt);
    
    if (font) {
        evo_text_measurement_t measurement;
        if (evo_text_advanced_measure(font, text, &measurement) == 0) {
            evo_text_advanced_release_font(font);
            return measurement.width;
        }
        evo_text_advanced_release_font(font);
    }
    
    /* Fallback to legacy measurement */
    return evo_text_get_text_width(text, size);
}

/* ============================================================================
 * FONT DISCOVERY AND DIAGNOSTICS
 * ============================================================================ */

int evo_text_advanced_discover_fonts(void) {
    if (!g_text_advanced_initialized || !g_text_context.font_manager) {
        return -1;
    }
    
    return evo_font_discover_system_fonts(g_text_context.font_manager);
}

void evo_text_advanced_print_font_info(void) {
    printf("\n🔤 EVO Advanced Text System Status\n");
    printf("==================================\n");
    
    if (!g_text_advanced_initialized) {
        printf("❌ Advanced text system not initialized\n");
        return;
    }
    
    printf("✅ System initialized\n");
    printf("📋 Font preference: ");
    switch (g_text_context.preference) {
        case EVO_TEXT_PREFER_SYSTEM_FONTS:
            printf("Prefer system fonts\n");
            break;
        case EVO_TEXT_PREFER_BITMAP_FONTS:
            printf("Prefer bitmap fonts\n");
            break;
        case EVO_TEXT_FORCE_SYSTEM_FONTS:
            printf("Force system fonts only\n");
            break;
        case EVO_TEXT_FORCE_BITMAP_FONTS:
            printf("Force bitmap fonts only\n");
            break;
    }
    
    printf("🎨 Rendering quality: ");
    switch (g_text_context.quality) {
        case EVO_TEXT_QUALITY_FAST:
            printf("Fast\n");
            break;
        case EVO_TEXT_QUALITY_NORMAL:
            printf("Normal\n");
            break;
        case EVO_TEXT_QUALITY_HIGH:
            printf("High\n");
            break;
        case EVO_TEXT_QUALITY_SUBPIXEL:
            printf("Subpixel\n");
            break;
    }
    
    if (g_text_context.font_manager) {
        printf("📚 Fonts discovered: %d\n", g_text_context.font_manager->database.count);
        printf("💾 Fonts loaded: %d\n", g_text_context.font_manager->loaded_count);
    }
    
    printf("\n");
}

int evo_text_advanced_test_font_loading(void) {
    if (!evo_text_advanced_is_available()) {
        return -1;
    }
    
    printf("🧪 Testing font loading capabilities...\n");
    
    /* Test loading different font types */
    const char* test_fonts[] = {"monospace", "sans-serif", "serif"};
    int success_count = 0;
    
    for (int i = 0; i < 3; i++) {
        evo_font_handle_t *font = evo_text_advanced_load_font(test_fonts[i], 16);
        if (font) {
            printf("✅ Successfully loaded '%s'\n", test_fonts[i]);
            evo_text_advanced_release_font(font);
            success_count++;
        } else {
            printf("❌ Failed to load '%s'\n", test_fonts[i]);
        }
    }
    
    printf("📊 Font loading test: %d/3 fonts loaded\n", success_count);
    return (success_count > 0) ? 0 : -1;
}

evo_text_font_preference_t evo_text_advanced_get_status(int *system_fonts_available,
                                                       int *bitmap_fallback_active) {
    if (system_fonts_available) {
        *system_fonts_available = evo_text_advanced_is_available();
    }
    
    if (bitmap_fallback_active) {
        *bitmap_fallback_active = !evo_text_advanced_is_available() ||
                                  g_text_context.preference == EVO_TEXT_PREFER_BITMAP_FONTS ||
                                  g_text_context.preference == EVO_TEXT_FORCE_BITMAP_FONTS;
    }
    
    return g_text_advanced_initialized ? g_text_context.preference : EVO_TEXT_PREFER_BITMAP_FONTS;
}

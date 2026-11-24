/*
 * EVO Fonts - Advanced Font Management Implementation
 * 
 * This implementation provides system font discovery, FreeType2 integration,
 * and comprehensive font management for EVO embedded systems.
 */

#include "evo_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

/* Global font manager instance */
static evo_font_manager_t g_font_manager = {0};
static int g_font_system_initialized = 0;

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Convert string to lowercase for comparison
 */
static void str_to_lower(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

/**
 * Check if string contains substring (case insensitive)
 */
static int str_contains(const char* haystack, const char* needle) {
    char h_lower[EVO_FONT_MAX_NAME];
    char n_lower[EVO_FONT_MAX_NAME];
    
    strncpy(h_lower, haystack, sizeof(h_lower) - 1);
    h_lower[sizeof(h_lower) - 1] = '\0';
    str_to_lower(h_lower);
    
    strncpy(n_lower, needle, sizeof(n_lower) - 1);
    n_lower[sizeof(n_lower) - 1] = '\0';
    str_to_lower(n_lower);
    
    return strstr(h_lower, n_lower) != NULL;
}

/* ============================================================================
 * FONT CLASSIFICATION FUNCTIONS
 * ============================================================================ */

/**
 * Classify font type from family name
 */
evo_font_type_t evo_font_classify_type(const char* family_name) {
    if (!family_name) return EVO_FONT_TYPE_UNKNOWN;
    
    /* Check for monospace indicators */
    if (str_contains(family_name, "mono") ||
        str_contains(family_name, "courier") ||
        str_contains(family_name, "console") ||
        str_contains(family_name, "fixed")) {
        return EVO_FONT_TYPE_MONOSPACE;
    }
    
    /* Check for serif indicators */
    if (str_contains(family_name, "serif") ||
        str_contains(family_name, "times") ||
        str_contains(family_name, "georgia")) {
        return EVO_FONT_TYPE_SERIF;
    }
    
    /* Check for display indicators */
    if (str_contains(family_name, "display") ||
        str_contains(family_name, "decorative") ||
        str_contains(family_name, "fancy")) {
        return EVO_FONT_TYPE_DISPLAY;
    }
    
    /* Check for symbol indicators */
    if (str_contains(family_name, "symbol") ||
        str_contains(family_name, "dingbat") ||
        str_contains(family_name, "icon")) {
        return EVO_FONT_TYPE_SYMBOL;
    }
    
    /* Default to sans-serif */
    return EVO_FONT_TYPE_SANS_SERIF;
}

/**
 * Parse font weight from style name
 */
evo_font_weight_t evo_font_parse_weight(const char* style_name) {
    if (!style_name) return EVO_FONT_WEIGHT_REGULAR;
    
    if (str_contains(style_name, "thin") || str_contains(style_name, "hairline")) {
        return EVO_FONT_WEIGHT_THIN;
    }
    if (str_contains(style_name, "light")) {
        return EVO_FONT_WEIGHT_LIGHT;
    }
    if (str_contains(style_name, "bold") || str_contains(style_name, "heavy")) {
        return EVO_FONT_WEIGHT_BOLD;
    }
    if (str_contains(style_name, "black") || str_contains(style_name, "ultra")) {
        return EVO_FONT_WEIGHT_BLACK;
    }
    if (str_contains(style_name, "medium") || str_contains(style_name, "demi")) {
        return EVO_FONT_WEIGHT_MEDIUM;
    }
    
    return EVO_FONT_WEIGHT_REGULAR;
}

/**
 * Parse font style from style name
 */
evo_font_style_t evo_font_parse_style(const char* style_name) {
    if (!style_name) return EVO_FONT_STYLE_NORMAL;
    
    if (str_contains(style_name, "italic")) {
        return EVO_FONT_STYLE_ITALIC;
    }
    if (str_contains(style_name, "oblique") || str_contains(style_name, "slant")) {
        return EVO_FONT_STYLE_OBLIQUE;
    }
    
    return EVO_FONT_STYLE_NORMAL;
}

/* ============================================================================
 * FONT MANAGER IMPLEMENTATION
 * ============================================================================ */

/**
 * Initialize font manager
 */
int evo_font_manager_init(evo_font_manager_t* manager) {
    if (!manager) return -1;
    
#ifdef HAVE_FREETYPE
    /* Initialize FreeType library */
    FT_Error error = FT_Init_FreeType(&manager->ft_library);
    if (error) {
        printf("ERROR: Failed to initialize FreeType library (error %d)\n", error);
        return -1;
    }
    printf("✅ FreeType2 library initialized\n");
#else
    printf("📋 FreeType2 not available - using bitmap fonts only\n");
    manager->ft_library = NULL;
#endif
    
    /* Initialize database */
    memset(&manager->database, 0, sizeof(manager->database));
    manager->database.initialized = 1;
    
    /* Initialize loaded fonts array */
    memset(manager->loaded_fonts, 0, sizeof(manager->loaded_fonts));
    manager->loaded_count = 0;
    
    printf("SUCCESS: Font manager initialized\n");
    return 0;
}

/**
 * Cleanup font manager
 */
void evo_font_manager_cleanup(evo_font_manager_t* manager) {
    if (!manager) return;
    
    /* Unload all loaded fonts */
    for (int i = 0; i < manager->loaded_count; i++) {
        if (manager->loaded_fonts[i]) {
            evo_font_unload(manager, manager->loaded_fonts[i]);
        }
    }
    
    /* Cleanup FreeType library */
    if (manager->ft_library) {
        FT_Done_FreeType(manager->ft_library);
        manager->ft_library = NULL;
    }
    
    /* Reset database */
    memset(&manager->database, 0, sizeof(manager->database));
    manager->loaded_count = 0;
    
    printf("Font manager cleaned up\n");
}

/**
 * Extract font information using FreeType
 */
static int extract_font_info(evo_font_manager_t* manager, const char* font_path, 
                           evo_font_info_t* info) {
    FT_Face face;
    FT_Error error;
    
    /* Load font face */
    error = FT_New_Face(manager->ft_library, font_path, 0, &face);
    if (error) {
        return -1;
    }
    
    /* Copy basic information */
    strncpy(info->path, font_path, EVO_FONT_MAX_PATH - 1);
    info->path[EVO_FONT_MAX_PATH - 1] = '\0';
    
    /* Extract family name */
    if (face->family_name) {
        strncpy(info->family, face->family_name, EVO_FONT_MAX_NAME - 1);
        info->family[EVO_FONT_MAX_NAME - 1] = '\0';
    } else {
        strcpy(info->family, "Unknown");
    }
    
    /* Extract style name */
    if (face->style_name) {
        strncpy(info->style_name, face->style_name, EVO_FONT_MAX_NAME - 1);
        info->style_name[EVO_FONT_MAX_NAME - 1] = '\0';
    } else {
        strcpy(info->style_name, "Regular");
    }
    
    /* Parse weight and style */
    info->weight = evo_font_parse_weight(info->style_name);
    info->style = evo_font_parse_style(info->style_name);
    info->type = evo_font_classify_type(info->family);
    
    /* Extract metrics */
    info->num_glyphs = face->num_glyphs;
    info->is_scalable = FT_IS_SCALABLE(face);
    info->has_kerning = FT_HAS_KERNING(face);
    
    /* Check Unicode support */
    info->has_unicode = 0;
    for (int i = 0; i < face->num_charmaps; i++) {
        if (face->charmaps[i]->encoding == FT_ENCODING_UNICODE) {
            info->has_unicode = 1;
            break;
        }
    }
    
    /* Character set support (simplified) */
    info->character_set = 0x01; /* Basic Latin */
    if (info->has_unicode) {
        info->character_set |= 0x02; /* Extended Latin */
    }
    
    /* Clean up */
    FT_Done_Face(face);
    
    return 0;
}

/**
 * Discover system fonts
 */
int evo_font_discover_system_fonts(evo_font_manager_t* manager) {
    if (!manager || !manager->database.initialized) return -1;
    
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[EVO_FONT_MAX_PATH];
    
    printf("Discovering system fonts in %s...\n", EVO_FONT_SYSTEM_DIR);
    
    dir = opendir(EVO_FONT_SYSTEM_DIR);
    if (!dir) {
        printf("WARNING: Could not open font directory %s\n", EVO_FONT_SYSTEM_DIR);
        return 0;
    }
    
    manager->database.count = 0;
    
    while ((entry = readdir(dir)) != NULL && 
           manager->database.count < EVO_FONT_MAX_FONTS) {
        
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        /* Check for font file extensions */
        if (!strstr(entry->d_name, ".ttf") && !strstr(entry->d_name, ".TTF") &&
            !strstr(entry->d_name, ".otf") && !strstr(entry->d_name, ".OTF")) {
            continue;
        }
        
        /* Build full path */
        snprintf(full_path, sizeof(full_path), "%s/%s", EVO_FONT_SYSTEM_DIR, entry->d_name);
        
        /* Check if it's a regular file */
        if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }
        
        /* Extract font information */
        evo_font_info_t* info = &manager->database.fonts[manager->database.count];
        if (extract_font_info(manager, full_path, info) == 0) {
            printf("  Found: %s (%s) - %s\n", info->family, info->style_name, entry->d_name);
            manager->database.count++;
        }
    }
    
    closedir(dir);
    
    printf("Discovered %d fonts\n", manager->database.count);
    return manager->database.count;
}

/**
 * Get font database
 */
const evo_font_database_t* evo_font_get_database(const evo_font_manager_t* manager) {
    if (!manager || !manager->database.initialized) return NULL;
    return &manager->database;
}

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
    
    return NULL;
}

/**
 * Find font by type and weight
 */
const evo_font_info_t* evo_font_find_by_type(const evo_font_manager_t* manager,
                                             evo_font_type_t type,
                                             evo_font_weight_t weight) {
    if (!manager) return NULL;
    
    const evo_font_info_t* best_match = NULL;
    int best_weight_diff = INT_MAX;
    
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

/**
 * Load a font
 */
evo_font_handle_t* evo_font_load(evo_font_manager_t* manager,
                                 const evo_font_info_t* font_info,
                                 int pixel_size) {
    if (!manager || !font_info || pixel_size <= 0) return NULL;
    
    /* Check if we can load more fonts */
    if (manager->loaded_count >= EVO_FONT_MAX_FONTS) {
        printf("ERROR: Maximum number of loaded fonts reached\n");
        return NULL;
    }
    
    /* Allocate font handle */
    evo_font_handle_t* handle = malloc(sizeof(evo_font_handle_t));
    if (!handle) return NULL;
    
    /* Load font face */
    FT_Error error = FT_New_Face(manager->ft_library, font_info->path, 0, &handle->face);
    if (error) {
        printf("ERROR: Failed to load font %s (error %d)\n", font_info->path, error);
        free(handle);
        return NULL;
    }
    
    /* Set pixel size */
    error = FT_Set_Pixel_Sizes(handle->face, 0, pixel_size);
    if (error) {
        printf("ERROR: Failed to set font size %d (error %d)\n", pixel_size, error);
        FT_Done_Face(handle->face);
        free(handle);
        return NULL;
    }
    
    /* Initialize handle */
    handle->info = (evo_font_info_t*)font_info;
    handle->current_size = pixel_size;
    handle->ref_count = 1;
    
    /* Add to loaded fonts list */
    manager->loaded_fonts[manager->loaded_count] = handle;
    manager->loaded_count++;
    
    printf("Loaded font: %s at %d pixels\n", font_info->family, pixel_size);
    return handle;
}

/**
 * Unload a font
 */
void evo_font_unload(evo_font_manager_t* manager, evo_font_handle_t* handle) {
    if (!manager || !handle) return;
    
    /* Decrease reference count */
    handle->ref_count--;
    if (handle->ref_count > 0) return;
    
    /* Remove from loaded fonts list */
    for (int i = 0; i < manager->loaded_count; i++) {
        if (manager->loaded_fonts[i] == handle) {
            /* Shift remaining elements */
            for (int j = i; j < manager->loaded_count - 1; j++) {
                manager->loaded_fonts[j] = manager->loaded_fonts[j + 1];
            }
            manager->loaded_count--;
            break;
        }
    }
    
    /* Cleanup FreeType face */
    if (handle->face) {
        FT_Done_Face(handle->face);
    }
    
    printf("Unloaded font: %s\n", handle->info->family);
    free(handle);
}

/* ============================================================================
 * GLOBAL FONT SYSTEM
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
    if (g_font_system_initialized) return 0;
    
    int result = evo_font_manager_init(&g_font_manager);
    if (result == 0) {
        g_font_system_initialized = 1;
        
        /* Discover system fonts automatically */
        evo_font_discover_system_fonts(&g_font_manager);
    }
    
    return result;
}

/**
 * Load a font (convenience wrapper)
 */
evo_font_handle_t* evo_font_load_font(evo_font_manager_t* manager,
                                      const evo_font_info_t* font_info,
                                      int pixel_size) {
    return evo_font_load(manager, font_info, pixel_size);
}

/**
 * Measure text width using FreeType2
 */
int evo_font_measure_text(evo_font_handle_t* handle, const char* text, int length) {
    if (!handle || !text) return -1;
    
#ifdef HAVE_FREETYPE
    /* Use strlen if length is -1 */
    if (length < 0) {
        length = strlen(text);
    }
    
    int total_width = 0;
    FT_GlyphSlot slot = handle->face->glyph;
    FT_Bool use_kerning = FT_HAS_KERNING(handle->face);
    FT_UInt previous_glyph = 0;
    
    for (int i = 0; i < length; i++) {
        /* Load glyph */
        FT_UInt glyph_index = FT_Get_Char_Index(handle->face, text[i]);
        FT_Error error = FT_Load_Glyph(handle->face, glyph_index, FT_LOAD_DEFAULT);
        if (error) continue;
        
        /* Add kerning if available */
        if (use_kerning && previous_glyph && glyph_index) {
            FT_Vector delta;
            FT_Get_Kerning(handle->face, previous_glyph, glyph_index,
                          FT_KERNING_DEFAULT, &delta);
            total_width += delta.x >> 6;  /* Convert from 26.6 fixed point */
        }
        
        /* Add glyph advance */
        total_width += slot->advance.x >> 6;  /* Convert from 26.6 fixed point */
        previous_glyph = glyph_index;
    }
    
    return total_width;
#else
    /* Simple fallback text measurement */
    if (length < 0) {
        length = strlen(text);
    }
    
    /* Estimate 8 pixels per character (bitmap font width) */
    return length * 8;
#endif
}

/**
 * Get font height
 */
int evo_font_get_height(evo_font_handle_t* handle) {
    if (!handle) return -1;
    
#ifdef HAVE_FREETYPE
    /* Return font height based on face metrics */
    return (handle->face->size->metrics.height >> 6);  /* Convert from 26.6 fixed point */
#else
    /* Fallback bitmap font height */
    return 16;  /* Typical bitmap font height */
#endif
}

/**
 * Release a font handle
 */
void evo_font_release(evo_font_handle_t* handle) {
    if (!handle) return;
    
    handle->ref_count--;
    if (handle->ref_count <= 0) {
        if (handle->face) {
            FT_Done_Face(handle->face);
        }
        free(handle);
    }
}

/**
 * Cleanup global font system
 */
void evo_font_global_cleanup(void) {
    if (!g_font_system_initialized) return;
    
    evo_font_manager_cleanup(&g_font_manager);
    g_font_system_initialized = 0;
}

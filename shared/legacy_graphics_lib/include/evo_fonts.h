/*
 * EVO Fonts - Advanced Font Management System
 * 
 * This header provides advanced font management capabilities for EVO systems,
 * including system font discovery, FreeType2 integration, and multi-font support.
 * 
 * Features:
 * - System font discovery and cataloging
 * - FreeType2 TTF/OTF font loading
 * - Font fallback chains
 * - Font caching and performance optimization
 * - Unicode character support
 */

#ifndef EVO_FONTS_H
#define EVO_FONTS_H

#include <stdint.h>
#include <stddef.h>

/* FreeType2 integration - conditional compilation */
#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#else
/* Dummy FreeType types when not available */
typedef void* FT_Library;
typedef void* FT_Face;
typedef int FT_Error;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * FONT SYSTEM CONSTANTS
 * ============================================================================ */

#define EVO_FONT_MAX_FONTS 32
#define EVO_FONT_MAX_PATH 512
#define EVO_FONT_MAX_NAME 128
#define EVO_FONT_SYSTEM_DIR "/usr/share/fonts"

/* Font weight enumeration */
typedef enum {
    EVO_FONT_WEIGHT_THIN = 100,
    EVO_FONT_WEIGHT_LIGHT = 300,
    EVO_FONT_WEIGHT_REGULAR = 400,
    EVO_FONT_WEIGHT_MEDIUM = 500,
    EVO_FONT_WEIGHT_BOLD = 700,
    EVO_FONT_WEIGHT_BLACK = 900
} evo_font_weight_t;

/* Font style enumeration */
typedef enum {
    EVO_FONT_STYLE_NORMAL = 0,
    EVO_FONT_STYLE_ITALIC,
    EVO_FONT_STYLE_OBLIQUE
} evo_font_style_t;

/* Font type enumeration */
typedef enum {
    EVO_FONT_TYPE_UNKNOWN = 0,
    EVO_FONT_TYPE_SERIF,
    EVO_FONT_TYPE_SANS_SERIF,
    EVO_FONT_TYPE_MONOSPACE,
    EVO_FONT_TYPE_DISPLAY,
    EVO_FONT_TYPE_SYMBOL
} evo_font_type_t;

/* ============================================================================
 * FONT DATA STRUCTURES
 * ============================================================================ */

/* Font information structure */
typedef struct {
    char path[EVO_FONT_MAX_PATH];           /* Full path to font file */
    char family[EVO_FONT_MAX_NAME];         /* Font family name */
    char style_name[EVO_FONT_MAX_NAME];     /* Style name (Bold, Italic, etc.) */
    evo_font_weight_t weight;               /* Font weight */
    evo_font_style_t style;                 /* Font style */
    evo_font_type_t type;                   /* Font type classification */
    int num_glyphs;                         /* Number of glyphs in font */
    int is_scalable;                        /* Whether font is scalable */
    int has_kerning;                        /* Whether font supports kerning */
    int has_unicode;                        /* Whether font supports Unicode */
    uint32_t character_set;                 /* Supported character sets (bitmask) */
} evo_font_info_t;

/* Font database structure */
typedef struct {
    evo_font_info_t fonts[EVO_FONT_MAX_FONTS];
    int count;
    int initialized;
} evo_font_database_t;

/* Font handle for loaded fonts */
typedef struct {
    FT_Face face;                           /* FreeType face handle */
    evo_font_info_t* info;                  /* Font information */
    int current_size;                       /* Current pixel size */
    int ref_count;                          /* Reference count */
} evo_font_handle_t;

/* Font manager context */
typedef struct {
    FT_Library ft_library;                  /* FreeType library instance */
    evo_font_database_t database;           /* Font database */
    evo_font_handle_t* loaded_fonts[EVO_FONT_MAX_FONTS];
    int loaded_count;
} evo_font_manager_t;

/* ============================================================================
 * FONT MANAGER FUNCTIONS
 * ============================================================================ */

/**
 * Initialize the font management system
 * @param manager Font manager context to initialize
 * @return 0 on success, negative on error
 */
int evo_font_manager_init(evo_font_manager_t* manager);

/**
 * Cleanup the font management system
 * @param manager Font manager context to cleanup
 */
void evo_font_manager_cleanup(evo_font_manager_t* manager);

/**
 * Discover and catalog all system fonts
 * @param manager Font manager context
 * @return Number of fonts discovered, negative on error
 */
int evo_font_discover_system_fonts(evo_font_manager_t* manager);

/**
 * Get font database
 * @param manager Font manager context
 * @return Pointer to font database
 */
const evo_font_database_t* evo_font_get_database(const evo_font_manager_t* manager);

/**
 * Find font by family name
 * @param manager Font manager context
 * @param family_name Font family name to search for
 * @return Pointer to font info, NULL if not found
 */
const evo_font_info_t* evo_font_find_by_family(const evo_font_manager_t* manager, 
                                               const char* family_name);

/**
 * Find font by type and weight
 * @param manager Font manager context
 * @param type Font type to search for
 * @param weight Desired font weight
 * @return Pointer to font info, NULL if not found
 */
const evo_font_info_t* evo_font_find_by_type(const evo_font_manager_t* manager,
                                             evo_font_type_t type,
                                             evo_font_weight_t weight);

/**
 * Load a font for rendering
 * @param manager Font manager context
 * @param font_info Font information from database
 * @param pixel_size Size in pixels
 * @return Font handle, NULL on error
 */
evo_font_handle_t* evo_font_load(evo_font_manager_t* manager,
                                 const evo_font_info_t* font_info,
                                 int pixel_size);

/**
 * Unload a font handle
 * @param manager Font manager context
 * @param handle Font handle to unload
 */
void evo_font_unload(evo_font_manager_t* manager, evo_font_handle_t* handle);

/**
 * Set font size
 * @param handle Font handle
 * @param pixel_size New size in pixels
 * @return 0 on success, negative on error
 */
int evo_font_set_size(evo_font_handle_t* handle, int pixel_size);

/**
 * Get glyph information
 * @param handle Font handle
 * @param character Unicode character
 * @param glyph_index Output glyph index
 * @return 0 on success, negative on error
 */
int evo_font_get_glyph_index(evo_font_handle_t* handle, uint32_t character, 
                            uint32_t* glyph_index);

/**
 * Load and render a glyph
 * @param handle Font handle
 * @param character Unicode character
 * @return 0 on success, negative on error
 */
int evo_font_load_glyph(evo_font_handle_t* handle, uint32_t character);

/**
 * Get kerning between two characters
 * @param handle Font handle
 * @param left_char Left character
 * @param right_char Right character
 * @param kerning Output kerning value (in 26.6 fixed point)
 * @return 0 on success, negative on error
 */
int evo_font_get_kerning(evo_font_handle_t* handle, uint32_t left_char, 
                        uint32_t right_char, int* kerning);

/* ============================================================================
 * FONT UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Get font type from family name
 * @param family_name Font family name
 * @return Font type classification
 */
evo_font_type_t evo_font_classify_type(const char* family_name);

/**
 * Get font weight from style name
 * @param style_name Font style name
 * @return Font weight value
 */
evo_font_weight_t evo_font_parse_weight(const char* style_name);

/**
 * Get font style from style name
 * @param style_name Font style name
 * @return Font style value
 */
evo_font_style_t evo_font_parse_style(const char* style_name);

/**
 * Check if character is supported by font
 * @param handle Font handle
 * @param character Unicode character
 * @return 1 if supported, 0 if not
 */
int evo_font_supports_character(evo_font_handle_t* handle, uint32_t character);

/**
 * Get font metrics
 * @param handle Font handle
 * @param ascender Output ascender value
 * @param descender Output descender value
 * @param line_height Output line height
 * @return 0 on success, negative on error
 */
int evo_font_get_metrics(evo_font_handle_t* handle, int* ascender, 
                        int* descender, int* line_height);

/**
 * Measure text width
 * @param handle Font handle
 * @param text Text to measure
 * @param length Text length (-1 for null-terminated)
 * @return Text width in pixels
 */
int evo_font_measure_text(evo_font_handle_t* handle, const char* text, int length);

/**
 * Get font height
 * @param handle Font handle
 * @return Font height in pixels
 */
int evo_font_get_height(evo_font_handle_t* handle);

/**
 * Load a font (convenience wrapper)
 * @param manager Font manager
 * @param font_info Font information
 * @param pixel_size Font size in pixels
 * @return Font handle or NULL on failure
 */
evo_font_handle_t* evo_font_load_font(evo_font_manager_t* manager,
                                      const evo_font_info_t* font_info,
                                      int pixel_size);

/**
 * Release a font handle
 * @param handle Font handle to release
 */
void evo_font_release(evo_font_handle_t* handle);

/* ============================================================================
 * GLOBAL FONT MANAGER
 * ============================================================================ */

/**
 * Get global font manager instance
 * @return Pointer to global font manager
 */
evo_font_manager_t* evo_font_get_manager(void);

/**
 * Initialize global font system
 * @return 0 on success, negative on error
 */
int evo_font_global_init(void);

/**
 * Cleanup global font system
 */
void evo_font_global_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_FONTS_H */

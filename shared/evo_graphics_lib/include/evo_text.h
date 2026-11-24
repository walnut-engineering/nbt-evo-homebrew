/**
 * @file evo_text.h
 * @brief EVO Unified Text Rendering (FreeType Only)
 * 
 * This module provides FreeType-based text rendering with system font support.
 * Replaces all previous text systems with a single, clean API.
 * 
 * Features:
 * - FreeType2 scalable font rendering
 * - System font loading from /usr/share/fonts/
 * - Font caching for performance
 * - Multiple font sizes and styles
 * - UTF-8 text support
 * 
 * @version 2.0.0
 * @date July 19, 2025
 */

#ifndef EVO_TEXT_H
#define EVO_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
#ifndef EVO_CONTEXT_T_DEFINED
#define EVO_CONTEXT_T_DEFINED
typedef struct evo_context evo_context_t;
#endif

/* ============================================================================
 * FONT SIZES AND STYLES
 * ============================================================================ */

/**
 * Standard font sizes (in points)
 */
typedef enum {
    EVO_FONT_TINY = 8,
    EVO_FONT_SMALL = 12,
    EVO_FONT_MEDIUM = 16,
    EVO_FONT_LARGE = 24,
    EVO_FONT_XLARGE = 32,
    EVO_FONT_XXLARGE = 48
} evo_font_size_t;

/**
 * Font styles and weights
 */
typedef enum {
    EVO_FONT_NORMAL = 0,
    EVO_FONT_BOLD = (1 << 0),
    EVO_FONT_ITALIC = (1 << 1),
    EVO_FONT_UNDERLINE = (1 << 2)
} evo_font_style_t;

/**
 * Text alignment options
 */
typedef enum {
    EVO_TEXT_ALIGN_LEFT = 0,
    EVO_TEXT_ALIGN_CENTER,
    EVO_TEXT_ALIGN_RIGHT
} evo_text_align_t;

/**
 * Font handle for cached fonts
 */
typedef struct evo_font evo_font_t;

/* ============================================================================
 * FONT MANAGEMENT
 * ============================================================================ */

/**
 * Initialize text rendering system
 * 
 * @return 0 on success, negative on error
 */
int evo_text_init(void);

/**
 * Shutdown text rendering system
 */
void evo_text_shutdown(void);

/**
 * Load font from system fonts
 * 
 * @param family Font family name (e.g., "Arial", "DejaVu Sans")
 * @param size Font size in points
 * @param style Font style flags
 * @return Font handle or NULL on error
 */
evo_font_t* evo_font_load(const char* family, int size, evo_font_style_t style);

/**
 * Load font from file
 * 
 * @param path Path to font file (.ttf, .otf, etc.)
 * @param size Font size in points
 * @param style Font style flags
 * @return Font handle or NULL on error
 */
evo_font_t* evo_font_load_from_file(const char* path, int size, evo_font_style_t style);

/**
 * Get default system font
 * 
 * @param size Font size in points
 * @return Font handle or NULL on error
 */
evo_font_t* evo_font_get_default(int size);

/**
 * Release font handle
 * 
 * @param font Font handle to release
 */
void evo_font_release(evo_font_t* font);

/* ============================================================================
 * TEXT RENDERING
 * ============================================================================ */

/**
 * Draw text using specific font
 * 
 * @param ctx Graphics context
 * @param font Font to use
 * @param text UTF-8 text string
 * @param x Text X position
 * @param y Text Y position (baseline)
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_text_draw(evo_context_t* ctx, evo_font_t* font, const char* text, int x, int y, uint32_t color);

/**
 * Draw text using standard font size
 * 
 * @param ctx Graphics context
 * @param text UTF-8 text string
 * @param x Text X position
 * @param y Text Y position (baseline)
 * @param size Standard font size
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_draw_text(evo_context_t* ctx, const char* text, int x, int y, int size, uint32_t color);

/**
 * Draw text with alignment
 * 
 * @param ctx Graphics context
 * @param font Font to use
 * @param text UTF-8 text string
 * @param x Text area X position
 * @param y Text area Y position
 * @param width Text area width (for alignment)
 * @param align Text alignment
 * @param color 32-bit RGBA color
 * @return 0 on success, negative on error
 */
int evo_text_draw_aligned(evo_context_t* ctx, evo_font_t* font, const char* text, int x, int y, int width, evo_text_align_t align, uint32_t color);

/**
 * Draw multiline text with word wrapping
 * 
 * @param ctx Graphics context
 * @param font Font to use
 * @param text UTF-8 text string (may contain \n)
 * @param x Text area X position
 * @param y Text area Y position
 * @param width Maximum text width (for wrapping)
 * @param line_height Line spacing (0 for auto)
 * @param color 32-bit RGBA color
 * @return Number of lines drawn, negative on error
 */
int evo_text_draw_multiline(evo_context_t* ctx, evo_font_t* font, const char* text, int x, int y, int width, int line_height, uint32_t color);

/* ============================================================================
 * TEXT MEASUREMENT
 * ============================================================================ */

/**
 * Get text width for layout calculations
 * 
 * @param font Font to measure with
 * @param text UTF-8 text string
 * @return Text width in pixels
 */
int evo_text_get_width(evo_font_t* font, const char* text);

/**
 * Get text height for layout calculations
 * 
 * @param font Font to measure with
 * @return Text height in pixels
 */
int evo_text_get_height(evo_font_t* font);

/**
 * Get text width using standard font size
 * 
 * @param text UTF-8 text string
 * @param size Standard font size
 * @return Text width in pixels
 */
int evo_text_width(const char* text, int size);

/**
 * Get text height using standard font size
 * 
 * @param size Standard font size
 * @return Text height in pixels
 */
int evo_text_height(int size);

/**
 * Get text bounding box
 * 
 * @param font Font to measure with
 * @param text UTF-8 text string
 * @param width Output: text width
 * @param height Output: text height
 * @param ascent Output: text ascent (above baseline)
 * @param descent Output: text descent (below baseline)
 * @return 0 on success, negative on error
 */
int evo_text_get_bounds(evo_font_t* font, const char* text, int* width, int* height, int* ascent, int* descent);

/* ============================================================================
 * FONT DISCOVERY
 * ============================================================================ */

/**
 * List available system fonts
 * 
 * @param max_fonts Maximum number of fonts to return
 * @param font_names Array to store font names (must be freed by caller)
 * @return Number of fonts found, negative on error
 */
int evo_font_list_system_fonts(int max_fonts, char*** font_names);

/**
 * Check if font family is available
 * 
 * @param family Font family name
 * @return 1 if available, 0 if not found
 */
int evo_font_is_available(const char* family);

/**
 * Get font metrics
 * 
 * @param font Font handle
 * @param line_height Output: recommended line height
 * @param ascent Output: font ascent
 * @param descent Output: font descent
 * @return 0 on success, negative on error
 */
int evo_font_get_metrics(evo_font_t* font, int* line_height, int* ascent, int* descent);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Clear font cache to free memory
 */
void evo_text_clear_cache(void);

/**
 * Get text rendering statistics
 * 
 * @param cached_fonts Output: number of cached fonts
 * @param memory_used Output: approximate memory usage in bytes
 * @return 0 on success, negative on error
 */
int evo_text_get_stats(int* cached_fonts, int* memory_used);

/**
 * Enable/disable text antialiasing
 * 
 * @param enabled 1 to enable, 0 to disable
 */
void evo_text_set_antialiasing(int enabled);

/**
 * Check if text system is initialized
 * 
 * @return 1 if initialized, 0 if not
 */
int evo_text_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_TEXT_H */

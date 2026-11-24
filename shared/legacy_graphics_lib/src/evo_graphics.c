#include "core/evo_graphics.h"
/*
 * EVO Graphics Library Implementation
 * 
 * Efficient 2D graphics drawing for EVO embedded systems.
 * Optimized for direct framebuffer manipulation with minimal overhead.
 * 
 * NOTE: Text rendering functions have been moved to evo_text.c library.
 * Include evo_text.h for text drawing capabilities.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

/* Simple integer square root for circle drawing */
static int isqrt(int n) {
    if (n < 2) return n;
    
    int x = n;
    int y = (x + 1) / 2;
    
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    
    return x;
}

/* ============================================================================
 * CORE GRAPHICS FUNCTIONS  
 * ============================================================================ */

evo_graphics_context_t* evo_graphics_create_context(uint32_t *buffer, int width, 
                                                   int height, int stride) {
    if (!buffer || width <= 0 || height <= 0) return NULL;
    
    evo_graphics_context_t *ctx = malloc(sizeof(evo_graphics_context_t));
    if (!ctx) return NULL;
    
    ctx->buffer = buffer;
    ctx->width = width;
    ctx->height = height;
    ctx->stride = stride;
    ctx->pixels = buffer;
    
    return ctx;
}

void evo_graphics_destroy_context(evo_graphics_context_t **ctx) {
    if (ctx && *ctx) {
        free(*ctx);
        *ctx = NULL;
    }
}

void evo_graphics_clear(evo_graphics_context_t *ctx, uint32_t color) {
    if (!ctx || !ctx->buffer) return;
    
    /* Convert color to hardware format */
    uint32_t hw_color = evo_graphics_convert_color(color);
    
    for (int i = 0; i < ctx->width * ctx->height; i++) {
        ctx->buffer[i] = hw_color;
    }
}

void evo_graphics_set_pixel(evo_graphics_context_t *ctx, int x, int y, uint32_t color) {
    if (!ctx || !ctx->buffer) return;
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;
    
    /* Convert color to hardware format */
    uint32_t hw_color = evo_graphics_convert_color(color);
    
    /* Use stride (in bytes) converted to pixel stride */
    int pixel_stride = ctx->stride / 4;  /* stride is in bytes, convert to 32-bit pixels */
    ctx->buffer[y * pixel_stride + x] = hw_color;
}

uint32_t evo_graphics_get_pixel(evo_graphics_context_t *ctx, int x, int y) {
    if (!ctx || !ctx->buffer) return 0;
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return 0;
    
    /* Use stride (in bytes) converted to pixel stride */
    int pixel_stride = ctx->stride / 4;  /* stride is in bytes, convert to 32-bit pixels */
    return ctx->buffer[y * pixel_stride + x];
}

/* ============================================================================
 * DRAWING PRIMITIVES
 * ============================================================================ */

void evo_graphics_draw_line(evo_graphics_context_t *ctx, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!ctx) return;
    
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        evo_graphics_set_pixel(ctx, x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void evo_graphics_draw_rectangle(evo_graphics_context_t *ctx, int x, int y, int width, int height, uint32_t color) {
    if (!ctx) return;
    
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            evo_graphics_set_pixel(ctx, x + i, y + j, color);
        }
    }
}

void evo_graphics_draw_rectangle_outline(evo_graphics_context_t *ctx, int x, int y, int width, int height, uint32_t color) {
    if (!ctx) return;
    
    /* Top and bottom lines */
    for (int i = 0; i < width; i++) {
        evo_graphics_set_pixel(ctx, x + i, y, color);
        evo_graphics_set_pixel(ctx, x + i, y + height - 1, color);
    }
    
    /* Left and right lines */
    for (int j = 0; j < height; j++) {
        evo_graphics_set_pixel(ctx, x, y + j, color);
        evo_graphics_set_pixel(ctx, x + width - 1, y + j, color);
    }
}

void evo_graphics_draw_circle(evo_graphics_context_t *ctx, int cx, int cy, int radius, uint32_t color) {
    if (!ctx || radius <= 0) return;
    
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        evo_graphics_set_pixel(ctx, cx + x, cy + y, color);
        evo_graphics_set_pixel(ctx, cx + y, cy + x, color);
        evo_graphics_set_pixel(ctx, cx - y, cy + x, color);
        evo_graphics_set_pixel(ctx, cx - x, cy + y, color);
        evo_graphics_set_pixel(ctx, cx - x, cy - y, color);
        evo_graphics_set_pixel(ctx, cx - y, cy - x, color);
        evo_graphics_set_pixel(ctx, cx + y, cy - x, color);
        evo_graphics_set_pixel(ctx, cx + x, cy - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void evo_graphics_draw_circle_filled(evo_graphics_context_t *ctx, int cx, int cy, int radius, uint32_t color) {
    if (!ctx || radius <= 0) return;
    
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                evo_graphics_set_pixel(ctx, cx + x, cy + y, color);
            }
        }
    }
}

void evo_graphics_draw_filled_circle(evo_graphics_context_t *ctx, int center_x, int center_y, 
                                   int radius, uint32_t color) {
    if (!ctx || radius <= 0) return;
    
    /* Draw filled circle by drawing horizontal lines */
    for (int y = -radius; y <= radius; y++) {
        int width = isqrt(radius * radius - y * y) * 2;
        if (width > 0) {
            evo_graphics_draw_rectangle(ctx, center_x - width/2, center_y + y, width, 1, color);
        }
    }
}

void evo_graphics_draw_checkerboard(evo_graphics_context_t *ctx, int x, int y, 
                                   int width, int height, int square_size, 
                                   uint32_t color1, uint32_t color2) {
    if (!ctx || !ctx->buffer) return;
    
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            int py = y + dy;
            
            if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                /* Determine which color based on checkerboard pattern */
                int square_x = dx / square_size;
                int square_y = dy / square_size;
                uint32_t color = ((square_x + square_y) % 2) ? color1 : color2;
                
                ctx->buffer[py * ctx->width + px] = color;
            }
        }
    }
}

/* ============================================================================
 * PROGRESS BAR FUNCTIONS (with proper text positioning)
 * ============================================================================ */

void evo_graphics_draw_progress_bar(evo_graphics_context_t *ctx, int x, int y, 
                                   int width, int height, float progress, 
                                   uint32_t bg_color, uint32_t fill_color) {
    if (!ctx || progress < 0.0f) return;
    
    /* Clamp progress to 0-1 range */
    if (progress > 1.0f) progress = 1.0f;
    
    /* Draw background */
    evo_graphics_draw_rectangle(ctx, x, y, width, height, bg_color);
    
    /* Draw progress fill */
    int fill_width = (int)(width * progress);
    if (fill_width > 0) {
        evo_graphics_draw_rectangle(ctx, x, y, fill_width, height, fill_color);
    }
    
    /* Draw border */
    evo_graphics_draw_rectangle_outline(ctx, x, y, width, height, EVO_COLOR_WHITE);
}

/* ============================================================================
 * TRANSPARENCY AND ALPHA FUNCTIONS
 * ============================================================================ */

void evo_graphics_clear_transparent(evo_graphics_context_t *ctx) {
    if (!ctx) return;
    evo_graphics_clear(ctx, EVO_COLOR_TRANSPARENT);
}

void evo_graphics_draw_alpha_overlay(evo_graphics_context_t *ctx, int x, int y, 
                                   int width, int height, uint32_t base_color, uint8_t alpha) {
    if (!ctx) return;
    
    uint32_t overlay_color = EVO_MAKE_ALPHA(base_color, alpha);
    evo_graphics_draw_rectangle(ctx, x, y, width, height, overlay_color);
}

void evo_graphics_create_gradient_transparency(evo_graphics_context_t *ctx, int x, int y, 
                                             int width, int height, uint32_t base_color,
                                             uint8_t start_alpha, uint8_t end_alpha, int vertical) {
    if (!ctx) return;
    
    for (int i = 0; i < (vertical ? height : width); i++) {
        uint8_t alpha = start_alpha + (uint8_t)((end_alpha - start_alpha) * i / (vertical ? height : width));
        uint32_t color = EVO_MAKE_ALPHA(base_color, alpha);
        
        if (vertical) {
            evo_graphics_draw_line(ctx, x, y + i, x + width - 1, y + i, color);
        } else {
            evo_graphics_draw_line(ctx, x + i, y, x + i, y + height - 1, color);
        }
    }
}

void evo_graphics_draw_alpha_gradient(evo_graphics_context_t *ctx, int x, int y, 
                                     int width, int height, uint32_t base_color,
                                     uint8_t start_alpha, uint8_t end_alpha, int horizontal) {
    if (!ctx || !ctx->buffer) return;
    
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            int py = y + dy;
            
            if (px < 0 || px >= ctx->width || py < 0 || py >= ctx->height) continue;
            
            /* Calculate alpha based on gradient direction */
            uint8_t alpha;
            if (horizontal) {
                alpha = start_alpha + ((end_alpha - start_alpha) * dx) / width;
            } else {
                alpha = start_alpha + ((end_alpha - start_alpha) * dy) / height;
            }
            
            uint32_t color = EVO_SET_ALPHA(base_color, alpha);
            uint32_t *pixel = (uint32_t*)ctx->buffer + py * (ctx->stride / 4) + px;
            *pixel = color;
        }
    }
}

void evo_graphics_draw_alpha_panel(evo_graphics_context_t *ctx, int x, int y, 
                                  int width, int height, uint32_t bg_color, uint32_t border_color,
                                  uint8_t bg_alpha, uint8_t border_alpha) {
    if (!ctx || !ctx->buffer) return;
    
    /* Draw background with alpha */
    evo_graphics_draw_alpha_overlay(ctx, x, y, width, height, bg_color, bg_alpha);
    
    /* Draw border with alpha */
    uint32_t border_with_alpha = EVO_SET_ALPHA(border_color, border_alpha);
    
    /* Top and bottom borders */
    evo_graphics_draw_rectangle(ctx, x, y, width, 2, border_with_alpha);
    evo_graphics_draw_rectangle(ctx, x, y + height - 2, width, 2, border_with_alpha);
    
    /* Left and right borders */
    evo_graphics_draw_rectangle(ctx, x, y, 2, height, border_with_alpha);
    evo_graphics_draw_rectangle(ctx, x + width - 2, y, 2, height, border_with_alpha);
}

/* ============================================================================
 * UTILITY FUNCTIONS (updated to use text library)
 * ============================================================================ */

void evo_graphics_draw_test_pattern(evo_graphics_context_t *ctx) {
    if (!ctx) return;
    
    /* Clear with dark background */
    evo_graphics_clear(ctx, EVO_COLOR_UI_BACKGROUND);
    
    /* Draw colored rectangles */
    evo_graphics_draw_rectangle(ctx, 50, 50, 100, 50, EVO_COLOR_RED);
    evo_graphics_draw_rectangle(ctx, 160, 50, 100, 50, EVO_COLOR_GREEN);
    evo_graphics_draw_rectangle(ctx, 270, 50, 100, 50, EVO_COLOR_BLUE);
    evo_graphics_draw_rectangle(ctx, 380, 50, 100, 50, EVO_COLOR_YELLOW);
    
    /* Draw circles */
    evo_graphics_draw_circle_filled(ctx, 100, 150, 30, EVO_COLOR_CYAN);
    evo_graphics_draw_circle_filled(ctx, 200, 150, 25, EVO_COLOR_MAGENTA);
    evo_graphics_draw_circle_filled(ctx, 300, 150, 35, EVO_COLOR_ORANGE);
    
    /* Draw lines */
    evo_graphics_draw_line(ctx, 0, 200, ctx->width, 200, EVO_COLOR_WHITE);
    evo_graphics_draw_line(ctx, 0, 220, ctx->width, 220, EVO_COLOR_PURPLE);
    
    /* Draw diagonal lines */
    evo_graphics_draw_line(ctx, 0, 0, ctx->width, ctx->height, EVO_COLOR_RED);
    evo_graphics_draw_line(ctx, ctx->width, 0, 0, ctx->height, EVO_COLOR_RED);
    
    /* Progress bar example (without text - text library should be used) */
    evo_graphics_draw_progress_bar(ctx, 50, 250, 300, 30, 0.65f, 
                                 EVO_COLOR_DARK_GRAY, EVO_COLOR_UI_SUCCESS);
    
    /* Note: For text rendering, use the evo_text library functions */
    /* Example: evo_text_draw_progress_text(ctx, 50, 250, 300, 30, "65%", EVO_FONT_MEDIUM, EVO_COLOR_WHITE); */
    
    /* Alpha transparency demo */
    evo_graphics_draw_alpha_overlay(ctx, 400, 250, 100, 80, EVO_COLOR_BLUE, EVO_ALPHA_SEMI);
    evo_graphics_create_gradient_transparency(ctx, 520, 250, 100, 80, EVO_COLOR_GREEN, 
                                            EVO_ALPHA_TRANSPARENT, EVO_ALPHA_OPAQUE, 1);
}

void evo_graphics_draw_border_markers(evo_graphics_context_t *ctx, int layer_x, int layer_y, 
                                     int layer_width, int layer_height) {
    if (!ctx) return;
    
    /* Window corners (full window) */
    evo_graphics_draw_rectangle(ctx, 0, 0, 20, 20, EVO_COLOR_RED);
    evo_graphics_draw_rectangle(ctx, ctx->width - 20, 0, 20, 20, EVO_COLOR_RED);
    evo_graphics_draw_rectangle(ctx, 0, ctx->height - 20, 20, 20, EVO_COLOR_RED);
    evo_graphics_draw_rectangle(ctx, ctx->width - 20, ctx->height - 20, 20, 20, EVO_COLOR_RED);
    
    /* Layer corners */
    evo_graphics_draw_rectangle(ctx, layer_x, layer_y, 15, 15, EVO_COLOR_YELLOW);
    evo_graphics_draw_rectangle(ctx, layer_x + layer_width - 15, layer_y, 15, 15, EVO_COLOR_YELLOW);
    evo_graphics_draw_rectangle(ctx, layer_x, layer_y + layer_height - 15, 15, 15, EVO_COLOR_YELLOW);
    evo_graphics_draw_rectangle(ctx, layer_x + layer_width - 15, layer_y + layer_height - 15, 15, 15, EVO_COLOR_YELLOW);
}

/* ============================================================================
 * PULSING EFFECTS
 * ============================================================================ */

void evo_graphics_draw_pulsing_alpha(evo_graphics_context_t *ctx, int x, int y, 
                                    int width, int height, uint32_t base_color,
                                    int pulse_frame, int pulse_speed) {
    if (!ctx || !ctx->buffer) return;
    
    /* Create pulsing effect using sine-like oscillation */
    int cycle = pulse_frame / pulse_speed;
    int alpha_range = 200; /* Pulse between 55 and 255 */
    int min_alpha = 55;
    
    /* Simple triangle wave for pulsing */
    int triangle_phase = cycle % 40;
    int alpha_offset;
    if (triangle_phase < 20) {
        alpha_offset = (triangle_phase * alpha_range) / 20;
    } else {
        alpha_offset = alpha_range - ((triangle_phase - 20) * alpha_range) / 20;
    }
    
    uint8_t alpha = min_alpha + alpha_offset;
    evo_graphics_draw_alpha_overlay(ctx, x, y, width, height, base_color, alpha);
}

/* ============================================================================
 * COLOR UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Blend two colors using alpha compositing
 * Formula: result = (src * src_alpha + dst * (1 - src_alpha)) / 255
 */
uint32_t evo_graphics_blend_colors(uint32_t src_color, uint32_t dst_color) {
    uint8_t src_alpha = EVO_GET_ALPHA(src_color);
    
    // If fully opaque, return source
    if (src_alpha == 255) return src_color;
    
    // If fully transparent, return destination
    if (src_alpha == 0) return dst_color;
    
    // Extract components
    uint8_t src_r = EVO_GET_RED(src_color);
    uint8_t src_g = EVO_GET_GREEN(src_color);
    uint8_t src_b = EVO_GET_BLUE(src_color);
    
    uint8_t dst_r = EVO_GET_RED(dst_color);
    uint8_t dst_g = EVO_GET_GREEN(dst_color);
    uint8_t dst_b = EVO_GET_BLUE(dst_color);
    uint8_t dst_a = EVO_GET_ALPHA(dst_color);
    
    // Alpha blend formula: result = (src * alpha + dst * (255 - alpha)) / 255
    uint8_t inv_alpha = 255 - src_alpha;
    uint8_t result_r = (src_r * src_alpha + dst_r * inv_alpha) / 255;
    uint8_t result_g = (src_g * src_alpha + dst_g * inv_alpha) / 255;
    uint8_t result_b = (src_b * src_alpha + dst_b * inv_alpha) / 255;
    
    // Result alpha is usually the destination alpha, or opaque for simplicity
    return EVO_COLOR_RGBA(result_r, result_g, result_b, dst_a);
}

/**
 * Draw a rectangle with proper alpha blending
 */
void evo_graphics_draw_rectangle_alpha_blended(evo_graphics_context_t *ctx, int x, int y, 
                                             int width, int height, uint32_t color) {
    if (!ctx || !ctx->buffer) return;
    
    uint8_t alpha = EVO_GET_ALPHA(color);
    
    // If fully opaque, use regular drawing
    if (alpha == 255) {
        evo_graphics_draw_rectangle(ctx, x, y, width, height, color);
        return;
    }
    
    // For each pixel, blend with existing background
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            int py = y + dy;
            
            if (px < 0 || px >= ctx->width || py < 0 || py >= ctx->height) continue;
            
            uint32_t *pixel_ptr = (uint32_t*)ctx->buffer + py * (ctx->stride / 4) + px;
            uint32_t bg_color = *pixel_ptr;
            uint32_t blended_color = evo_graphics_blend_colors(color, bg_color);
            *pixel_ptr = blended_color;
        }
    }
}

void evo_graphics_set_buffer(evo_graphics_context_t *ctx, uint32_t *buffer) {
    if (ctx && buffer) {
        ctx->buffer = buffer;
        ctx->pixels = buffer;  /* Update pixels pointer as well */
    }
}

/**
 * Draw filled circle with alpha blending
 * Uses the same circle algorithm but with alpha compositing for each pixel
 */
void evo_graphics_draw_filled_circle_alpha(evo_graphics_context_t *ctx, int center_x, int center_y, int radius, uint32_t color, uint8_t alpha) {
    if (!ctx || !ctx->buffer || radius <= 0) return;
    
    // Extract source color components (ARGB8888 format)
    uint8_t src_r = EVO_GET_RED(color);
    uint8_t src_g = EVO_GET_GREEN(color);
    uint8_t src_b = EVO_GET_BLUE(color);
    
    int x = 0;
    int y = radius;
    int p = 1 - radius;  // Decision parameter for circle algorithm
    
    // Draw filled circle using modified Bresenham algorithm
    while (x <= y) {
        // Fill horizontal lines for each octant with alpha blending
        
        // Bottom and top lines
        for (int px = center_x - x; px <= center_x + x; px++) {
            // Bottom line
            int line_y1 = center_y + y;
            if (line_y1 >= 0 && line_y1 < ctx->height && px >= 0 && px < ctx->width) {
                int index = line_y1 * (ctx->stride / 4) + px;
                uint32_t dst_color = ctx->buffer[index];
                uint8_t dst_r = EVO_GET_RED(dst_color);
                uint8_t dst_g = EVO_GET_GREEN(dst_color);
                uint8_t dst_b = EVO_GET_BLUE(dst_color);
                
                uint8_t result_r = (src_r * alpha + dst_r * (255 - alpha)) / 255;
                uint8_t result_g = (src_g * alpha + dst_g * (255 - alpha)) / 255;
                uint8_t result_b = (src_b * alpha + dst_b * (255 - alpha)) / 255;
                
                ctx->buffer[index] = EVO_MAKE_COLOR(result_r, result_g, result_b, 255);
            }
            
            // Top line
            int line_y2 = center_y - y;
            if (line_y2 >= 0 && line_y2 < ctx->height && px >= 0 && px < ctx->width) {
                int index = line_y2 * (ctx->stride / 4) + px;
                uint32_t dst_color = ctx->buffer[index];
                uint8_t dst_r = EVO_GET_RED(dst_color);
                uint8_t dst_g = EVO_GET_GREEN(dst_color);
                uint8_t dst_b = EVO_GET_BLUE(dst_color);
                
                uint8_t result_r = (src_r * alpha + dst_r * (255 - alpha)) / 255;
                uint8_t result_g = (src_g * alpha + dst_g * (255 - alpha)) / 255;
                uint8_t result_b = (src_b * alpha + dst_b * (255 - alpha)) / 255;
                
                ctx->buffer[index] = EVO_MAKE_COLOR(result_r, result_g, result_b, 255);
            }
        }
        
        // Left and right side lines
        for (int px = center_x - y; px <= center_x + y; px++) {
            // Right side line
            int line_y3 = center_y + x;
            if (line_y3 >= 0 && line_y3 < ctx->height && px >= 0 && px < ctx->width) {
                int index = line_y3 * (ctx->stride / 4) + px;
                uint32_t dst_color = ctx->buffer[index];
                uint8_t dst_r = EVO_GET_RED(dst_color);
                uint8_t dst_g = EVO_GET_GREEN(dst_color);
                uint8_t dst_b = EVO_GET_BLUE(dst_color);
                
                uint8_t result_r = (src_r * alpha + dst_r * (255 - alpha)) / 255;
                uint8_t result_g = (src_g * alpha + dst_g * (255 - alpha)) / 255;
                uint8_t result_b = (src_b * alpha + dst_b * (255 - alpha)) / 255;
                
                ctx->buffer[index] = EVO_MAKE_COLOR(result_r, result_g, result_b, 255);
            }
            
            // Left side line  
            int line_y4 = center_y - x;
            if (line_y4 >= 0 && line_y4 < ctx->height && px >= 0 && px < ctx->width) {
                int index = line_y4 * (ctx->stride / 4) + px;
                uint32_t dst_color = ctx->buffer[index];
                uint8_t dst_r = EVO_GET_RED(dst_color);
                uint8_t dst_g = EVO_GET_GREEN(dst_color);
                uint8_t dst_b = EVO_GET_BLUE(dst_color);
                
                uint8_t result_r = (src_r * alpha + dst_r * (255 - alpha)) / 255;
                uint8_t result_g = (src_g * alpha + dst_g * (255 - alpha)) / 255;
                uint8_t result_b = (src_b * alpha + dst_b * (255 - alpha)) / 255;
                
                ctx->buffer[index] = EVO_MAKE_COLOR(result_r, result_g, result_b, 255);
            }
        }
        
        if (p < 0) {
            p += 2 * x + 1;
        } else {
            p += 2 * (x - y) + 1;
            y--;
        }
        x++;
    }
}

/* Graphics library ends here */

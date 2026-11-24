/*
 * DOOM Video Driver for EVO - EVO Graphics Library v2.0 Implementation
 *
 * This implementation migrates from the legacy evo_display_helper library
 * to the modern EVO Graphics Library v2.0 with hardware acceleration support.
 *
 * Migration Notes:
 * - Uses evo_graphics_lib.h single include
 * - Hardware acceleration via GAL/PVR2D backends
 * - Unified API with evo_context_t
 * - Cairo-powered graphics rendering
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <stdarg.h>

#include "doomdef.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

/* EVO Graphics Library v2.0 - Single include for all functionality */
#include "evo_graphics_lib.h"
#include "evo_context.h"   /* evo_context_get_display() */
#include "evo_display.h"   /* evo_display_enable_presentation_mode() */

/* DOOM screen dimensions */
#define DOOM_SCREEN_WIDTH   320
#define DOOM_SCREEN_HEIGHT  200

/* Display dimensions (scaled up for automotive displays) */
#define DISPLAY_WIDTH       1440
#define DISPLAY_HEIGHT      540

/* Global state */
static evo_context_t *g_evo_context = NULL;
static byte doom_palette[256][3];  /* DOOM's RGB palette */
static int graphics_initialized = 0;

/* Double buffering support */
static uint32_t *buffer_memory[2] = {NULL, NULL};  /* Two render buffers */
static int current_buffer = 0;                     /* Index of current render buffer */
static int double_buffering_enabled = 0;           /* Flag indicating if double buffering is active */

/* Display layout: full screen (HMI hidden), floating picture-in-picture, or
 * docked beside the HMI. Selected with -pip [x y w h] or -dock. */
#define DOOM_LAYOUT_FULL  0
#define DOOM_LAYOUT_PIP   1
#define DOOM_LAYOUT_DOCK  2
static int display_layout = DOOM_LAYOUT_FULL;

/* The car HMI's base layer on the EVO head unit (only visible layer at boot).
 * Override with -hmilayer <id>. */
#define EVO_HMI_BASE_LAYER 2147483648u

/* Where the 320x200 game picture sits inside the 1440x540 window */
#define GAME_SRC_X 288
#define GAME_SRC_Y 0
#define GAME_SRC_W 864
#define GAME_SRC_H 540

/* On-screen frame rate counter (-showfps): tiny 3x5 glyphs drawn into the
 * 320x200 game screen before it is scaled, using the brightest palette entry. */
static int show_fps = 0;
static byte fps_color = 4;
static const unsigned char glyph3x5[][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},   /* 0-4 */
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},   /* 5-9 */
    {7,4,6,4,4}, {7,5,7,4,4}, {7,4,7,1,7}, {0,0,0,0,2}                 /* F P S . */
};

static void draw_glyph(byte *scr, int x, int y, int g, int scale, byte color)
{
    int row, col, i, j;
    for (row = 0; row < 5; row++) {
        for (col = 0; col < 3; col++) {
            if (glyph3x5[g][row] & (4 >> col)) {
                for (i = 0; i < scale; i++) {
                    for (j = 0; j < scale; j++) {
                        int px = x + col * scale + j, py = y + row * scale + i;
                        if (px >= 0 && px < SCREENWIDTH && py >= 0 && py < SCREENHEIGHT) {
                            scr[py * SCREENWIDTH + px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_text(byte *scr, int x, int y, const char *text, int scale, byte color)
{
    for (; *text; text++) {
        int g = -1;
        if (*text >= '0' && *text <= '9') g = *text - '0';
        else if (*text == 'F') g = 10;
        else if (*text == 'P') g = 11;
        else if (*text == 'S') g = 12;
        else if (*text == '.') g = 13;
        if (g >= 0) {
            draw_glyph(scr, x, y, g, scale, color);
        }
        x += 4 * scale;
    }
}

/* Hardware acceleration tracking */
static int hardware_acceleration_enabled = 0;
static evo_backend_type_t active_backend = EVO_BACKEND_SOFTWARE;

/* Function prototypes */
static void convert_doom_screen_to_rgba(byte *doom_screen, uint32_t *rgba_buffer, int width, int height, int stride);
static void doom_save_screenshot_bmp(const char *filename);
void I_GetEvent(void);

/* Input system prototypes */
void I_InitDirectInput(void);
void I_ShutdownDirectInput(void);
void I_GetEvents(void);

/* Global FPS tracking - using integer math only for ARM7TDMI compatibility */
static int current_fps_x10 = 0;  /* FPS * 10 to avoid floating point */

/* Clean shutdown on SIGTERM/SIGINT: the flag is picked up by I_FinishUpdate on the
 * main thread, which calls I_Quit() so the display library restores the car HMI.
 * The test runner stops the game with SIGTERM, so this is the normal exit path. */
static volatile sig_atomic_t quit_requested = 0;
static void doom_termination_handler(int sig)
{
    (void)sig;
    quit_requested = 1;
}

/* Letterbox borders must be opaque: with alpha 0 the layer manager would blend
 * whatever sits below the game window into the side bars. */
static void fill_opaque_black(uint32_t *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        p[i] = 0xFF000000u;
    }
}

/* Debug flag to help diagnose SIGILL (illegal instruction) issue */
#ifdef DEBUG_SIGILL
#define DEBUG_PRINT(fmt, ...) printf("DEBUG [%s:%d]: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
// Custom signal handler for debugging
static void sigill_handler(int sig) {
    fprintf(stderr, "SIGILL (Illegal Instruction) caught in DOOM engine!\n");
    fprintf(stderr, "This is likely due to an ARM architecture mismatch or alignment issue\n");
    
    // Print the signal number and stack trace info
    fprintf(stderr, "Attempting to dump stack (if available)...\n");
    fflush(stderr);
    
    // Sleep to ensure message is output
    usleep(10000);
    
    // Exit with error code
    exit(128 + sig);
}
#else
#define DEBUG_PRINT(fmt, ...)
#endif

/**
 * Get the current frames per second
 */
int I_GetFPS(void)
{
    return current_fps_x10 / 10;  /* Return integer FPS */
}

/**
 * Initialize graphics system using EVO Graphics Library v2.0
 */
void I_InitGraphics(void)
{
    printf("[DOOM] Initializing EVO Graphics Library v2.0...\n");
    fflush(stdout);
    
    #ifdef DEBUG_SIGILL
    // Install custom SIGILL handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigill_handler;
    sigaction(SIGILL, &sa, NULL);
    DEBUG_PRINT("Installed custom SIGILL handler for debugging");
    #endif
    
    if (graphics_initialized) {
        printf("[DOOM] Graphics already initialized\n");
        fflush(stdout);
        return;
    }
    
    /* Initialize EVO Graphics Library v2.0 */
    printf("[DOOM] Initializing EVO Graphics Library...\n");
    if (evo_graphics_init() != 0) {
        printf("[DOOM] ERROR: Failed to initialize EVO Graphics Library\n");
        I_Error("Failed to initialize EVO Graphics Library");
        return;
    }
    
    printf("[DOOM] EVO Graphics Library v%s initialized\n", evo_graphics_get_version());
    
    /* Create graphics context */
    printf("[DOOM] Creating graphics context (%dx%d)...\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    g_evo_context = evo_context_create(DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    
    if (!g_evo_context) {
        printf("[DOOM] ERROR: Failed to create EVO graphics context\n");
        evo_graphics_shutdown();
        I_Error("Failed to create EVO graphics context");
        return;
    }
    
    /* Show the context window */
    if (evo_context_show(g_evo_context) != 0) {
        printf("[DOOM] ERROR: Failed to show graphics context\n");
        evo_context_destroy(g_evo_context);
        evo_graphics_shutdown();
        I_Error("Failed to show graphics context");
        return;
    }
    
    /* Check hardware acceleration status */
    hardware_acceleration_enabled = evo_context_is_hardware_accelerated(g_evo_context);
    active_backend = evo_context_get_backend(g_evo_context);
    
    printf("[DOOM] Graphics context created successfully\n");
    printf("[DOOM] Hardware acceleration: %s\n", hardware_acceleration_enabled ? "ENABLED" : "DISABLED");
    printf("[DOOM] Active backend: %s\n", evo_backend_get_name(active_backend));
    
    /* Enable hardware blit if available */
    if (hardware_acceleration_enabled) {
        printf("[DOOM] Enabling hardware blit acceleration...\n");
        if (evo_context_enable_hardware_blit(g_evo_context) == 0) {
            printf("[DOOM] Hardware blit acceleration enabled\n");
        } else {
            printf("[DOOM] Warning: Failed to enable hardware blit acceleration\n");
        }
    }

    /* Layout. Full screen hides the car HMI layers while the game runs;
     * evo_context_destroy() (via I_ShutdownGraphics) restores exactly the layers
     * that were visible before. -pip keeps the HMI and floats the game picture in
     * a corner; -dock scales the HMI into the left part of the screen and puts the
     * game picture beside it, so both stay usable. */
    {
        evo_display_t *disp = evo_context_get_display(g_evo_context);
        uint32_t hmi_layer = EVO_HMI_BASE_LAYER;
        int p = M_CheckParm("-hmilayer");
        if (p && p < myargc - 1) {
            hmi_layer = (uint32_t)strtoul(myargv[p + 1], NULL, 10);
        }
        if (M_CheckParm("-dock")) {
            display_layout = DOOM_LAYOUT_DOCK;
        } else if (M_CheckParm("-pip")) {
            display_layout = DOOM_LAYOUT_PIP;
        }

        if (!disp) {
            printf("[DOOM] Warning: no display handle, layout control unavailable\n");
        } else if (display_layout == DOOM_LAYOUT_PIP) {
            int w = 432, h = 270;
            int x = DISPLAY_WIDTH - w - 16, y = DISPLAY_HEIGHT - h - 16;
            p = M_CheckParm("-pip");
            if (p && p + 4 < myargc && myargv[p + 1][0] >= '0' && myargv[p + 1][0] <= '9') {
                x = atoi(myargv[p + 1]);
                y = atoi(myargv[p + 2]);
                w = atoi(myargv[p + 3]);
                h = atoi(myargv[p + 4]);
            }
            evo_display_set_layer_geometry(disp, GAME_SRC_X, GAME_SRC_Y, GAME_SRC_W, GAME_SRC_H, x, y, w, h);
            printf("[DOOM] Layout: picture-in-picture at %d,%d %dx%d, HMI stays visible\n", x, y, w, h);
        } else if (display_layout == DOOM_LAYOUT_DOCK) {
            /* HMI keeps its 8:3 aspect at two thirds width, centred vertically; the
             * game picture (8:5) fills the remaining third beside it. */
            evo_display_set_foreign_layer_rect(disp, hmi_layer, 0, 90, 960, 360);
            evo_display_set_layer_geometry(disp, GAME_SRC_X, GAME_SRC_Y, GAME_SRC_W, GAME_SRC_H, 960, 120, 480, 300);
            printf("[DOOM] Layout: docked beside HMI layer %u\n", hmi_layer);
        } else {
            if (evo_display_enable_presentation_mode(disp) == 0) {
                printf("[DOOM] Layout: full screen - HMI layers hidden until exit\n");
            } else {
                printf("[DOOM] Warning: could not enable presentation mode\n");
            }
        }
    }

    show_fps = M_CheckParm("-showfps") != 0;
    if (show_fps) {
        printf("[DOOM] On-screen FPS counter enabled\n");
    }

    /* Quit cleanly when the test runner or a user sends SIGTERM/SIGINT */
    signal(SIGTERM, doom_termination_handler);
    signal(SIGINT, doom_termination_handler);
    
    /* Initialize DOOM's screen buffers */
    screens[0] = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!screens[0]) {
        printf("[DOOM] ERROR: Failed to allocate screen buffer\n");
        evo_context_destroy(g_evo_context);
        evo_graphics_shutdown();
        I_Error("Failed to allocate screen buffer");
        return;
    }
    
    /* Clear screen buffer */
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);
    
    graphics_initialized = 1;
    printf("[DOOM] Graphics initialization complete - graphics_initialized=%d\n", graphics_initialized);
    fflush(stdout);
    
    /* Initialize input system */
    I_InitDirectInput();
    
    /* Initialize double buffering */
    printf("[DOOM] Setting up double buffering...\n");
    fflush(stdout);
    
    /* Get framebuffer info for buffer size calculation */
    int context_width = evo_context_get_width(g_evo_context);
    int context_height = evo_context_get_height(g_evo_context);
    int buffer_size = context_width * context_height; /* 32-bit RGBA pixels */
    
    /* Create two identical buffers for double buffering */
    buffer_memory[0] = (uint32_t *)malloc(buffer_size * sizeof(uint32_t));
    buffer_memory[1] = (uint32_t *)malloc(buffer_size * sizeof(uint32_t));
    
    if (!buffer_memory[0] || !buffer_memory[1]) {
        printf("[DOOM] Warning - Failed to allocate double buffers, using single buffering\n");
        fflush(stdout);
        
        /* Clean up if one buffer was allocated but not the other */
        if (buffer_memory[0]) {
            free(buffer_memory[0]);
            buffer_memory[0] = NULL;
        }
        
        if (buffer_memory[1]) {
            free(buffer_memory[1]);
            buffer_memory[1] = NULL;
        }
        
        double_buffering_enabled = 0;
        printf("[DOOM] Using single buffering mode\n");
        fflush(stdout);
    } else {
        /* Initialize both buffers to black */
        memset(buffer_memory[0], 0, buffer_size * sizeof(uint32_t));
        memset(buffer_memory[1], 0, buffer_size * sizeof(uint32_t));
        
        /* Start with the first buffer */
        current_buffer = 0;
        double_buffering_enabled = 1;
        
        printf("[DOOM] Double buffering successfully enabled\n");
        fflush(stdout);
    }
    
    printf("[DOOM] Graphics initialization complete\n");
    printf("[DOOM] Ready for rendering at %dx%d -> %dx%d\n", 
           DOOM_SCREEN_WIDTH, DOOM_SCREEN_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    fflush(stdout);
}

/**
 * Shutdown graphics system
 */
void I_ShutdownGraphics(void)
{
    printf("[DOOM] Shutting down graphics...\n");
    
    if (!graphics_initialized) {
        return;
    }
    
    /* Shutdown input system */
    I_ShutdownDirectInput();
    
    /* Free DOOM screen buffer */
    if (screens[0]) {
        free(screens[0]);
        screens[0] = NULL;
    }
    
    /* Free both buffers if we're in double buffering mode */
    if (double_buffering_enabled) {
        if (buffer_memory[0]) {
            free(buffer_memory[0]);
            buffer_memory[0] = NULL;
        }
        
        if (buffer_memory[1]) {
            free(buffer_memory[1]);
            buffer_memory[1] = NULL;
        }
        
        printf("[DOOM] Double buffering resources freed\n");
    }
    
    /* Destroy EVO graphics context */
    if (g_evo_context) {
        evo_context_destroy(g_evo_context);
        g_evo_context = NULL;
    }

    /* Shutdown EVO Graphics Library */
    evo_graphics_shutdown();

    graphics_initialized = 0;
    hardware_acceleration_enabled = 0;

    printf("[DOOM] Graphics shutdown complete\n");
    fflush(stdout);
    
    /* CRITICAL: Exit immediately like the shared version did */
    /* This prevents DOOM from continuing execution after graphics shutdown */
    _exit(0);
}/**
 * Set DOOM's color palette
 */
void I_SetPalette(byte *palette)
{
    int i;
    static int palette_update_count = 0;
    
    /* Convert DOOM's palette format (256 entries of RGB bytes) to our format */
    for (i = 0; i < 256; i++) {
        doom_palette[i][0] = palette[i * 3 + 0]; /* Red */
        doom_palette[i][1] = palette[i * 3 + 1]; /* Green */  
        doom_palette[i][2] = palette[i * 3 + 2]; /* Blue */
    }
    
    /* Brightest palette entry for the on-screen FPS counter */
    {
        int best = 0, best_sum = -1;
        for (i = 0; i < 256; i++) {
            int sum = doom_palette[i][0] + doom_palette[i][1] + doom_palette[i][2];
            if (sum > best_sum) {
                best_sum = sum;
                best = i;
            }
        }
        fps_color = (byte)best;
    }

    /* Only print palette update message occasionally to reduce spam */
    palette_update_count++;
    if (palette_update_count == 1 || palette_update_count % 100 == 0) {
        printf("[DOOM] Palette updated (%d times)\n", palette_update_count);
    }
}

/**
 * Convert DOOM's 8-bit paletized screen to 32-bit RGBA for display
 * Optimized version with letterboxing and hardware acceleration support
 */
static void convert_doom_screen_to_rgba(byte *doom_screen, uint32_t *rgba_buffer, int width, int height, int stride_pixels)
{
    int x, y;
    
    /* Letterboxing - Center 864x540 DOOM content within 1440x540 buffer */
    const int letterbox_width = 864;
    const int letterbox_height = 540;
    const int letterbox_x_offset = 288; /* (1440-864)/2 = 288 */
    
    /* Pre-calculated scaling lookup tables for maximum speed */
    static int doom_x_lookup[864] = {0}; /* DOOM X coordinate for each display X */
    static int doom_y_lookup[540] = {0}; /* DOOM Y coordinate for each display Y */
    static int lookup_initialized = 0;
    
    /* Initialize lookup tables once */
    if (!lookup_initialized) {
        for (x = 0; x < letterbox_width; x++) {
            doom_x_lookup[x] = (x * DOOM_SCREEN_WIDTH) / letterbox_width;
            if (doom_x_lookup[x] >= DOOM_SCREEN_WIDTH) 
                doom_x_lookup[x] = DOOM_SCREEN_WIDTH - 1;
        }
        for (y = 0; y < letterbox_height; y++) {
            doom_y_lookup[y] = (y * DOOM_SCREEN_HEIGHT) / letterbox_height;
            if (doom_y_lookup[y] >= DOOM_SCREEN_HEIGHT) 
                doom_y_lookup[y] = DOOM_SCREEN_HEIGHT - 1;
        }
        lookup_initialized = 1;
    }
    
    /* Clear letterbox borders efficiently - only once per frame */
    for (y = 0; y < height; y++) {
        uint32_t *row_start = rgba_buffer + y * stride_pixels;
        /* Left and right borders: opaque black */
        fill_opaque_black(row_start, letterbox_x_offset);
        fill_opaque_black(row_start + letterbox_x_offset + letterbox_width, letterbox_x_offset);
    }
    
    /* Ultra-fast rendering loop - no divisions, all lookups */
    for (y = 0; y < letterbox_height; y++) {
        /* Get DOOM source row using lookup table */
        int doom_y = doom_y_lookup[y];
        byte *doom_row = doom_screen + doom_y * DOOM_SCREEN_WIDTH;
        uint32_t *display_row = rgba_buffer + y * stride_pixels + letterbox_x_offset;
        
        /* Process 4 pixels at a time when possible for better performance */
        int x;
        for (x = 0; x < letterbox_width - 3; x += 4) {
            /* Process 4 pixels in parallel */
            int doom_x0 = doom_x_lookup[x];
            int doom_x1 = doom_x_lookup[x + 1];
            int doom_x2 = doom_x_lookup[x + 2];
            int doom_x3 = doom_x_lookup[x + 3];
            
            byte pal0 = doom_row[doom_x0];
            byte pal1 = doom_row[doom_x1];
            byte pal2 = doom_row[doom_x2];
            byte pal3 = doom_row[doom_x3];
            
            /* Convert to ARGB format for EVO Graphics Library */
            display_row[x]     = 0xFF000000 | (doom_palette[pal0][0] << 16) | (doom_palette[pal0][1] << 8) | doom_palette[pal0][2];
            display_row[x + 1] = 0xFF000000 | (doom_palette[pal1][0] << 16) | (doom_palette[pal1][1] << 8) | doom_palette[pal1][2];
            display_row[x + 2] = 0xFF000000 | (doom_palette[pal2][0] << 16) | (doom_palette[pal2][1] << 8) | doom_palette[pal2][2];
            display_row[x + 3] = 0xFF000000 | (doom_palette[pal3][0] << 16) | (doom_palette[pal3][1] << 8) | doom_palette[pal3][2];
        }
        
        /* Handle remaining pixels */
        for (; x < letterbox_width; x++) {
            int doom_x = doom_x_lookup[x];
            byte palette_index = doom_row[doom_x];
            
            /* Convert to ARGB format */
            display_row[x] = 0xFF000000 | (doom_palette[palette_index][0] << 16) | 
                           (doom_palette[palette_index][1] << 8) | doom_palette[palette_index][2];
        }
    }
}

/**
 * Update the display with current DOOM screen content
 */
void I_FinishUpdate(void)
{
    static int frame_count = 0;
    static struct timeval last_time = {0};
    struct timeval current_time;
    long time_diff_ms;
    
    /* Debug output for first few frames */
    if (frame_count < 5) {
        printf("DEBUG: I_FinishUpdate called - frame %d\n", frame_count + 1);
        fflush(stdout);
    }
    
    if (quit_requested) {
        printf("[DOOM] Termination signal received - shutting down cleanly\n");
        fflush(stdout);
        I_Quit();
    }
    
    /* Get current time for FPS calculation */
    gettimeofday(&current_time, NULL);
    
    /* Calculate time difference in milliseconds */
    if (last_time.tv_sec != 0) {
        time_diff_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 + 
                       (current_time.tv_usec - last_time.tv_usec) / 1000;
        
        if (time_diff_ms > 0) {
            /* Integer-only FPS calculation to avoid floating point on ARM7TDMI */
            int instantaneous_fps_x10 = 10000 / time_diff_ms;  /* FPS * 10 */
            current_fps_x10 = (current_fps_x10 * 19 + instantaneous_fps_x10) / 20;  /* Moving average */
        }
    }
    
    /* Store current time for next frame */
    last_time = current_time;
    
    frame_count++;
    
    #ifdef DEBUG_SIGILL
    DEBUG_PRINT("Starting frame %d update", frame_count);
    DEBUG_PRINT("Checking graphics_initialized=%d, g_evo_context=%p", 
                graphics_initialized, g_evo_context);
    #endif
    
    if (!graphics_initialized || !g_evo_context) {
        if (frame_count < 5) {
            printf("DEBUG: I_FinishUpdate early return - graphics_initialized=%d, g_evo_context=%p\n", 
                   graphics_initialized, g_evo_context);
            fflush(stdout);
        }
        return;
    }
    
    /* Print scaling info on first frame only */
    if (frame_count == 1) {
        printf("INFO: Rendering %dx%d DOOM content letterboxed to 864x540 within %dx%d display\n", 
               DOOM_SCREEN_WIDTH, DOOM_SCREEN_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        
        if (buffer_memory[1]) {
            printf("INFO: Double buffering active\n");
        }
    }
    
    if (show_fps && screens[0]) {
        char fps_text[16];
        snprintf(fps_text, sizeof(fps_text), "FPS %d.%d", current_fps_x10 / 10, current_fps_x10 % 10);
        draw_text(screens[0], 2, 2, fps_text, 2, fps_color);
    }

    /* Get context dimensions */
    int context_width = evo_context_get_width(g_evo_context);
    int context_height = evo_context_get_height(g_evo_context);
    
    /* Check if double buffering is available */
    if (double_buffering_enabled && buffer_memory[0] && buffer_memory[1]) {
        /* Get the current back buffer to render to */
        uint32_t *render_buffer = buffer_memory[current_buffer];
        
        /* Convert DOOM screen directly to the back buffer */
        convert_doom_screen_to_rgba(screens[0], render_buffer, context_width, context_height, context_width);
    
        /* Get framebuffer from EVO context and copy our buffer to it */
        uint32_t *framebuffer = evo_context_get_framebuffer(g_evo_context);
        if (framebuffer) {
            memcpy(framebuffer, render_buffer, context_width * context_height * sizeof(uint32_t));
        }
        
        /* Present the frame using EVO Graphics Library */
        if (evo_context_present(g_evo_context) != 0) {
            printf("[DOOM] Warning - display present failed for frame %d\n", frame_count);
            return;
        }
        
        /* Use the other buffer for next frame's rendering */
        current_buffer = 1 - current_buffer;  /* Toggle between 0 and 1 */
        
    } else {
        /* Single buffering fallback */
        /* Get framebuffer directly from EVO context */
        uint32_t *framebuffer = evo_context_get_framebuffer(g_evo_context);
        if (framebuffer) {
            /* Convert DOOM screen directly to the framebuffer */
            convert_doom_screen_to_rgba(screens[0], framebuffer, context_width, context_height, context_width);
        }
        
        /* Present the frame using EVO Graphics Library */
        if (evo_context_present(g_evo_context) != 0) {
            printf("[DOOM] Warning - display present failed for frame %d\n", frame_count);
            return;
        }
    }
    
    /* Output FPS info at startup and occasionally */
    if (frame_count == 1 || frame_count % 300 == 0) {
        printf("[DOOM] Frame %d - FPS: %d.%d, Backend: %s, HW Accel: %s\n", 
               frame_count, current_fps_x10 / 10, current_fps_x10 % 10,
               evo_backend_get_name(active_backend),
               hardware_acceleration_enabled ? "ON" : "OFF");
    }
}

/**
 * Save a screenshot in BMP format for debugging
 */
static void doom_save_screenshot_bmp(const char *filename)
{
    /* TODO: Implement simple BMP writing if needed for debugging */
    /* For now, just log that we would save a screenshot */
    printf("[DOOM] Would save screenshot to %s\n", filename);
}

/**
 * Read current screen content back to buffer (required by DOOM)
 */
void I_ReadScreen(byte *scr)
{
    if (screens[0] && scr) {
        memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
    }
}

/**
 * Update screen without page flipping (stub - not needed for EVO)
 */
void I_UpdateNoBlit(void)
{
    /* Nothing to do - our implementation always updates immediately */
}

/**
 * Start frame timing (called at beginning of each frame)
 */
void I_StartFrame(void)
{
    /* Original DOOM I_StartFrame was typically empty or very simple */
    /* Frame timing is handled by TryRunTics and I_GetTime */
}

/**
 * Start input polling (called to poll for input events)
 */
void I_StartTic(void)
{
    /* Poll for input events */
    I_GetEvent();
}

/**
 * Get input events (delegates to EVO HID input system)
 */
void I_GetEvent(void)
{
    /* Call our EVO HID input system */
    I_GetEvents();
}

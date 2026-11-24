/*
 * DOOM Video Driver for EVO - Shared Library Implementation
 *
 * This implementation uses the shared evo_display_helper library
 * for simplified Screen API + ILM integration, replacing the complex
 * manual implementation with a clean, maintainable interface.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>  /* For performance timing */

#include "doomdef.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

/* Include the shared library */
#include "../../shared/include/evo_display_helper.h"

/* DOOM screen dimensions */
#define DOOM_SCREEN_WIDTH   320
#define DOOM_SCREEN_HEIGHT  200

/* Display dimensions (scaled up for automotive displays) */
#define DISPLAY_WIDTH       1440
#define DISPLAY_HEIGHT      540

/* Safe surface/layer IDs (using shared library recommended ranges) */
#define DOOM_SURFACE_ID     2100  /* Safe range: 2000-2999 */
#define DOOM_LAYER_ID       10000 /* High priority layer */

/* Global state */
static evo_display_context_t *g_display_ctx = NULL;
static byte doom_palette[256][3];  /* DOOM's RGB palette */
static int graphics_initialized = 0;

/* Double buffering support */
static char *buffer_memory[2] = {NULL, NULL};  /* Two render buffers */
static int current_buffer = 0;                 /* Index of current render buffer */
static int double_buffering_enabled = 0;       /* Flag indicating if double buffering is active */

/* Function prototypes */
static void convert_doom_screen_to_rgba(byte *doom_screen, char *rgba_buffer, int stride);
static void doom_save_screenshot_bmp(const char *filename);
void I_GetEvent(void);

/* Input system prototypes */
void I_InitDirectInput(void);
void I_ShutdownDirectInput(void);
void I_GetEvents(void);

/* Debug flag to help diagnose SIGILL (illegal instruction) issue */
#ifdef DEBUG_SIGILL
#define DEBUG_PRINT(fmt, ...) printf("DEBUG [%s:%d]: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
// Custom signal handler for debugging
static void sigill_handler(int sig) {
    fprintf(stderr, "SIGILL (Illegal Instruction) caught in DOOM engine!\n");
    fprintf(stderr, "This is likely due to an ARM architecture mismatch or alignment issue\n");
    
    // Print the signal number and stack trace info
    void *array[20];
    size_t size;
    char **strings;
    size_t i;

    // Attempt to get stack trace
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

/* Global FPS tracking - using integer math only for ARM7TDMI compatibility */
static int current_fps_x10 = 0;  /* FPS * 10 to avoid floating point */

/**
 * Get the current frames per second
 */
int I_GetFPS(void)
{
    return current_fps_x10 / 10;  /* Return integer FPS */
}

/**
 * Initialize graphics system using shared library
 */
void I_InitGraphics(void)
{
    printf("INFO: Initializing graphics system\n");
    fflush(stdout);
    
    #ifdef DEBUG_SIGILL
    // Install custom SIGILL handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigill_handler;
    sigaction(SIGILL, &sa, NULL);
    DEBUG_PRINT("Installed custom SIGILL handler for debugging");
    
    // Print architecture and compiler information for debugging
    DEBUG_PRINT("Architecture info: %s", 
        #if defined(__ARM_ARCH_4T__)
            "ARM v4T"
        #elif defined(__ARM_ARCH_5T__)
            "ARM v5T"
        #elif defined(__ARM_ARCH_6T2__)
            "ARM v6T2"
        #elif defined(__ARM_ARCH_7A__)
            "ARM v7A"
        #elif defined(__ARM_ARCH_7R__)
            "ARM v7R"
        #elif defined(__ARM_ARCH_7M__)
            "ARM v7M"
        #else
            "Unknown ARM architecture"
        #endif
    );
    
    DEBUG_PRINT("Compiled with: %s %s", __VERSION__, __DATE__);
    DEBUG_PRINT("Checking alignment requirements...");
    #endif
    
    if (graphics_initialized) {
        printf("INFO: Graphics already initialized\n");
        fflush(stdout);
        return;
    }
    
    /* Create display context using shared library */
    g_display_ctx = evo_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 
                                       DOOM_SURFACE_ID, DOOM_LAYER_ID);
    
    if (!g_display_ctx) {
        printf("ERROR: Failed to create display context\n");
        fflush(stdout);
        I_Error("Failed to create display context");
        return;
    }
    
    printf("INFO: Display context created (%dx%d)\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    fflush(stdout);
    
    /* Show the display */
    if (evo_display_show(g_display_ctx) != 0) {
        printf("DOOM: ERROR - evo_display_show failed\n");
        fflush(stdout);
        I_Error("DOOM: Failed to show display");
        return;
    }
    
    /* Initialize DOOM's screen buffers */
    screens[0] = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!screens[0]) {
        printf("DOOM: ERROR - Failed to allocate screen buffer\n");
        fflush(stdout);
        I_Error("DOOM: Failed to allocate screen buffer");
        return;
    }
    
    /* Clear screen buffer */
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);
    
    graphics_initialized = 1;
    printf("DOOM: Graphics initialization complete - graphics_initialized=%d\n", graphics_initialized);
    fflush(stdout);
    
    /* Initialize input system */
    I_InitDirectInput();
    
    /* Initialize our own double buffering since the library doesn't support it */
    printf("DOOM: Setting up manual double buffering...\n");
    fflush(stdout);
    
    if (g_display_ctx && g_display_ctx->screen_ptr) {
        int buffer_size = DISPLAY_HEIGHT * g_display_ctx->screen_stride;
        
        /* Create two identical buffers for double buffering */
        buffer_memory[0] = (char *)malloc(buffer_size);
        buffer_memory[1] = (char *)malloc(buffer_size);
        
        if (!buffer_memory[0] || !buffer_memory[1]) {
            printf("DOOM: Warning - Failed to allocate double buffers, using single buffering\n");
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
            
            /* Fall back to single buffering using the display's buffer */
            buffer_memory[0] = g_display_ctx->screen_ptr;
            double_buffering_enabled = 0;
            
            printf("DOOM: Using single buffering mode\n");
            fflush(stdout);
        } else {
            /* Initialize both buffers to black */
            memset(buffer_memory[0], 0, buffer_size);
            memset(buffer_memory[1], 0, buffer_size);
            
            /* Start with the first buffer */
            current_buffer = 0;
            double_buffering_enabled = 1;
            
            printf("DOOM: Double buffering successfully enabled\n");
            fflush(stdout);
        }
    } else {
        printf("DOOM: Warning - Invalid display context, cannot initialize buffers\n");
        fflush(stdout);
        double_buffering_enabled = 0;
    }
    
    printf("DOOM: Graphics initialization complete\n");
    fflush(stdout);
}

/**
 * Shutdown graphics system
 */
void I_ShutdownGraphics(void)
{
    printf("DOOM: Shutting down graphics...\n");
    
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
        
        printf("DOOM: Double buffering resources freed\n");
    }
    
    /* Destroy display context (handles all cleanup automatically) */
    if (g_display_ctx) {
        evo_display_destroy_and_exit(&g_display_ctx, 0);
        /* This function doesn't return - it calls _exit() */
    }
    
    graphics_initialized = 0;
}

/**
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
    
    /* Only print palette update message occasionally to reduce spam */
    palette_update_count++;
    if (palette_update_count == 1 || palette_update_count % 100 == 0) {
        printf("DOOM: Palette updated (%d times)\n", palette_update_count);
    }
}

/**
 * Convert DOOM's 8-bit paletized screen to 32-bit RGBA for display
 * 
 * MAXIMUM PERFORMANCE VERSION: ARM-safe with lookup table optimization
 */
static void convert_doom_screen_to_rgba(byte *doom_screen, char *rgba_buffer, int stride)
{
    int x, y;
    byte palette_index;
    
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
    for (y = 0; y < DISPLAY_HEIGHT; y++) {
        char *row_start = rgba_buffer + y * stride;
        /* Clear left border */
        memset(row_start, 0, letterbox_x_offset * 4);
        /* Clear right border */
        memset(row_start + (letterbox_x_offset + letterbox_width) * 4, 0, letterbox_x_offset * 4);
    }
    
    /* Ultra-fast rendering loop - no divisions, all lookups */
    for (y = 0; y < letterbox_height; y++) {
        /* Get DOOM source row using lookup table */
        int doom_y = doom_y_lookup[y];
        byte *doom_row = doom_screen + doom_y * DOOM_SCREEN_WIDTH;
        char *display_row = rgba_buffer + y * stride + letterbox_x_offset * 4;
        
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
            
            char *pixel0 = display_row + x * 4;
            char *pixel1 = pixel0 + 4;
            char *pixel2 = pixel0 + 8;
            char *pixel3 = pixel0 + 12;
            
            /* Write 4 pixels - EVO BGRA order */
            pixel0[0] = doom_palette[pal0][2]; pixel0[1] = doom_palette[pal0][1]; 
            pixel0[2] = doom_palette[pal0][0]; pixel0[3] = 0xFF;
            
            pixel1[0] = doom_palette[pal1][2]; pixel1[1] = doom_palette[pal1][1]; 
            pixel1[2] = doom_palette[pal1][0]; pixel1[3] = 0xFF;
            
            pixel2[0] = doom_palette[pal2][2]; pixel2[1] = doom_palette[pal2][1]; 
            pixel2[2] = doom_palette[pal2][0]; pixel2[3] = 0xFF;
            
            pixel3[0] = doom_palette[pal3][2]; pixel3[1] = doom_palette[pal3][1]; 
            pixel3[2] = doom_palette[pal3][0]; pixel3[3] = 0xFF;
        }
        
        /* Handle remaining pixels */
        for (; x < letterbox_width; x++) {
            int doom_x = doom_x_lookup[x];
            palette_index = doom_row[doom_x];
            
            char *pixel = display_row + x * 4;
            pixel[0] = doom_palette[palette_index][2]; /* Blue */
            pixel[1] = doom_palette[palette_index][1]; /* Green */
            pixel[2] = doom_palette[palette_index][0]; /* Red */
            pixel[3] = 0xFF;                           /* Alpha */
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
    DEBUG_PRINT("Checking graphics_initialized=%d, g_display_ctx=%p", 
                graphics_initialized, g_display_ctx);
    #endif
    
    if (!graphics_initialized || !g_display_ctx) {
        if (frame_count < 5) {
            printf("DEBUG: I_FinishUpdate early return - graphics_initialized=%d, g_display_ctx=%p\n", 
                   graphics_initialized, g_display_ctx);
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
    
    /* Check if double buffering is available */
    if (double_buffering_enabled && buffer_memory[0] && buffer_memory[1]) {
        /* Get the current back buffer to render to */
        char *render_buffer = buffer_memory[current_buffer];
        
        /* Convert DOOM screen directly to the back buffer */
        convert_doom_screen_to_rgba(screens[0], render_buffer, g_display_ctx->screen_stride);
    
        /* Fast copy from back buffer to display buffer */
        memcpy(g_display_ctx->screen_ptr, render_buffer, 
               DISPLAY_HEIGHT * g_display_ctx->screen_stride);
        
        /* Update the display using shared library */
        if (evo_display_update(g_display_ctx) != 0) {
            printf("DOOM: Warning - display update failed for frame %d\n", frame_count);
            return;
        }
        
        /* Use the other buffer for next frame's rendering */
        current_buffer = 1 - current_buffer;  /* Toggle between 0 and 1 */
        
    } else {
        /* Single buffering fallback */
        /* Convert DOOM screen directly to the display buffer */
        convert_doom_screen_to_rgba(screens[0], g_display_ctx->screen_ptr, g_display_ctx->screen_stride);
        
        /* Update the display using shared library */
        if (evo_display_update(g_display_ctx) != 0) {
            printf("DOOM: Warning - display update failed for frame %d\n", frame_count);
            return;
        }
    }
    
    /* Output FPS info at startup and occasionally */
    if (frame_count == 1 || frame_count % 300 == 0) {
        printf("DOOM: Frame %d - FPS: %d.%d\n", frame_count, current_fps_x10 / 10, current_fps_x10 % 10);
    }
}

/**
 * Save a screenshot in BMP format for debugging
 */
static void doom_save_screenshot_bmp(const char *filename)
{
    /* TODO: Implement simple BMP writing if needed for debugging */
    /* For now, just log that we would save a screenshot */
    printf("DOOM: Would save screenshot to %s\n", filename);
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

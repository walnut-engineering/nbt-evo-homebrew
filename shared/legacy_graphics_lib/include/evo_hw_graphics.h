/*
 * EVO Hardware Graphics - EGL/OpenGL ES Integration
 * 
 * This library extends the EVO Display Helper with hardware-accelerated graphics
 * capabilities using EGL and OpenGL ES. It provides seamless integration with
 * the existing Screen API + ILM infrastructure while adding GPU acceleration.
 * 
 * Based on EVO target system graphics configuration:
 * - EGL Display 1: PowerVR SGX540rev120 (for 1440x540 display)
 *   Libraries: libIMGegl.so, libImgGLESv2.so, libImgGLESv1_CM.so
 *   Support: libusc.so, libsrv_um.so, libpvr2d.so, pvrsrv.so
 * - EGL Display 2: Vivante GC320 (secondary/video - libGalcore.so)
 * 
 * Graphics Pipeline Configuration:
 * - Display 1: 1440x540 @ 60Hz (HDMI1/main display)
 * - Display 2: 800x480 @ 60Hz (OTHER1/secondary)
 * - Format: RGBA8888/RGBX8888 with hardware BGRA layout compensation
 * - Memory: 4MB contiguous GPU memory + dynamic allocation
 * - Pipelines: GFX=1, Video1=1, Video2=2, Video3=2
 * 
 * Key Features:
 * - Hardware-accelerated 3D graphics via OpenGL ES 1.x/2.0
 * - Seamless integration with existing evo_display_helper
 * - Automatic fallback to software rendering if hardware fails
 * - Uses target-specific EGL libraries (libIMGegl.so, libImgGLESv2.so)
 * - PowerVR SGX540-optimized initialization and rendering
 * 
 * Usage:
 * 1. Create display context with evo_display_create() as usual
 * 2. Initialize hardware graphics with evo_hw_graphics_init()
 * 3. Use OpenGL ES calls for 3D rendering
 * 4. Use evo_hw_graphics_present() to display results
 * 5. Falls back automatically to software rendering if needed
 */

#ifndef EVO_HW_GRAPHICS_H
#define EVO_HW_GRAPHICS_H

#include "evo_display_helper.h"

/* Forward declarations for EGL/OpenGL ES types (no static linking) */
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativeWindowType;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef unsigned int EGLbitfield;

/* OpenGL ES forward declarations */
typedef float GLfloat;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef unsigned char GLboolean;

/* EGL constants */
#define EGL_NO_DISPLAY          ((EGLDisplay)0)
#define EGL_NO_CONTEXT          ((EGLContext)0)
#define EGL_NO_SURFACE          ((EGLSurface)0)
#define EGL_DEFAULT_DISPLAY     ((EGLNativeDisplayType)0)

/* EGL configuration constants */
#define EGL_SURFACE_TYPE            0x3033
#define EGL_WINDOW_BIT              0x0004
#define EGL_RENDERABLE_TYPE         0x3040
#define EGL_OPENGL_ES_BIT           0x0001
#define EGL_OPENGL_ES2_BIT          0x0004
#define EGL_RED_SIZE                0x3024
#define EGL_GREEN_SIZE              0x3023
#define EGL_BLUE_SIZE               0x3022
#define EGL_ALPHA_SIZE              0x3021
#define EGL_DEPTH_SIZE              0x3025
#define EGL_STENCIL_SIZE            0x3026
#define EGL_SAMPLES                 0x3031
#define EGL_NONE                    0x3038

/* EGL context constants */
#define EGL_CONTEXT_CLIENT_VERSION  0x3098
#define EGL_OPENGL_ES_API           0x30A0

/* EGL boolean values */
#define EGL_TRUE                    1
#define EGL_FALSE                   0

/* OpenGL ES constants */
#define GL_VERSION              0x1F02
#define GL_EXTENSIONS           0x1F03
#define GL_NO_ERROR             0
#define GL_COLOR_BUFFER_BIT     0x00004000
#define GL_DEPTH_BUFFER_BIT     0x00000100

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * HARDWARE GRAPHICS CONTEXT
 * ============================================================================ */

/* Hardware graphics capabilities */
typedef enum {
    EVO_HW_GRAPHICS_NONE = 0,
    EVO_HW_GRAPHICS_GLES1 = 1,
    EVO_HW_GRAPHICS_GLES2 = 2,
    EVO_HW_GRAPHICS_BOTH = 3
} evo_hw_graphics_caps_t;

/* Hardware graphics rendering mode */
typedef enum {
    EVO_HW_RENDER_SOFTWARE_ONLY = 0,    /* Software-only rendering (fallback) */
    EVO_HW_RENDER_HARDWARE_ONLY = 1,    /* Hardware-only rendering */
    EVO_HW_RENDER_HYBRID = 2            /* Mix of hardware and software */
} evo_hw_render_mode_t;

/* EGL display configuration for EVO target system */
typedef struct {
    int egl_display_id;         /* EGL display to use (1=PowerVR SGX540, 2=Vivante GC320) */
    int prefer_gles_version;    /* Preferred OpenGL ES version (1 or 2) */
    int enable_vsync;           /* Enable vertical sync */
    int enable_multisampling;   /* Enable anti-aliasing */
    int samples;                /* Number of MSAA samples (2, 4, 8) */
    int depth_buffer_bits;      /* Depth buffer size (16, 24) */
    int stencil_buffer_bits;    /* Stencil buffer size (0, 8) */
} evo_hw_graphics_config_t;

/* Hardware graphics context - extends evo_display_context */
typedef struct {
    /* Associated display context */
    evo_display_context_t *display;
    
    /* Hardware capabilities */
    evo_hw_graphics_caps_t capabilities;
    evo_hw_render_mode_t render_mode;
    
    /* EGL state */
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    int egl_display_id;         /* 1=PowerVR SGX540, 2=Vivante GC320 */
    
    /* OpenGL ES state */
    int gles_version;           /* 1 or 2 */
    int viewport_width;
    int viewport_height;
    
    /* Configuration */
    evo_hw_graphics_config_t config;
    
    /* Performance tracking */
    unsigned long frames_rendered;
    unsigned long hw_render_calls;
    unsigned long sw_render_calls;
    
    /* Error tracking */
    int last_egl_error;
    int last_gl_error;
    char error_message[256];
    
} evo_hw_graphics_context_t;

/* ============================================================================
 * MAIN API FUNCTIONS
 * ============================================================================ */

/**
 * Initialize hardware graphics acceleration for an existing display context.
 * This extends the display context with EGL/OpenGL ES capabilities.
 * 
 * @param display_ctx Existing display context from evo_display_create()
 * @param config Hardware graphics configuration (NULL for defaults)
 * @return Hardware graphics context or NULL on failure
 */
evo_hw_graphics_context_t* evo_hw_graphics_init(evo_display_context_t *display_ctx,
                                                const evo_hw_graphics_config_t *config);

/**
 * Create default hardware graphics configuration for EVO target system.
 * Uses EGL display 1 (PowerVR SGX540) with OpenGL ES 2.0 by default.
 * 
 * @return Default configuration structure
 */
evo_hw_graphics_config_t evo_hw_graphics_default_config(void);

/**
 * Begin hardware-accelerated rendering frame.
 * Sets up OpenGL ES context and viewport for drawing.
 * 
 * @param hw_ctx Hardware graphics context
 * @return 0 on success, -1 on failure
 */
int evo_hw_graphics_begin_frame(evo_hw_graphics_context_t *hw_ctx);

/**
 * Present hardware-rendered frame to the display.
 * Swaps EGL buffers and updates the Screen API display.
 * 
 * @param hw_ctx Hardware graphics context
 * @return 0 on success, -1 on failure
 */
int evo_hw_graphics_present_frame(evo_hw_graphics_context_t *hw_ctx);

/**
 * Destroy hardware graphics context and clean up EGL resources.
 * The associated display context remains valid for software rendering.
 * 
 * @param hw_ctx Hardware graphics context (will be set to NULL)
 */
void evo_hw_graphics_destroy(evo_hw_graphics_context_t **hw_ctx);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Check if hardware graphics is available and working.
 * 
 * @param hw_ctx Hardware graphics context
 * @return 1 if hardware graphics is available, 0 if not
 */
int evo_hw_graphics_is_available(evo_hw_graphics_context_t *hw_ctx);

/**
 * Get hardware graphics capabilities.
 * 
 * @param hw_ctx Hardware graphics context
 * @return Capability flags
 */
evo_hw_graphics_caps_t evo_hw_graphics_get_capabilities(evo_hw_graphics_context_t *hw_ctx);

/**
 * Set rendering mode (hardware-only, software-only, or hybrid).
 * 
 * @param hw_ctx Hardware graphics context
 * @param mode Rendering mode
 * @return 0 on success, -1 on failure
 */
int evo_hw_graphics_set_render_mode(evo_hw_graphics_context_t *hw_ctx, 
                                   evo_hw_render_mode_t mode);

/**
 * Get the last EGL error for debugging.
 * 
 * @param hw_ctx Hardware graphics context
 * @return EGL error code
 */
int evo_hw_graphics_get_egl_error(evo_hw_graphics_context_t *hw_ctx);

/**
 * Get the last OpenGL ES error for debugging.
 * 
 * @param hw_ctx Hardware graphics context
 * @return OpenGL ES error code
 */
int evo_hw_graphics_get_gl_error(evo_hw_graphics_context_t *hw_ctx);

/**
 * Get error message string for debugging.
 * 
 * @param hw_ctx Hardware graphics context
 * @return Error message string
 */
const char* evo_hw_graphics_get_error(evo_hw_graphics_context_t *hw_ctx);

/**
 * Print hardware graphics information for debugging.
 * 
 * @param hw_ctx Hardware graphics context
 * @param prefix Prefix string for output
 */
void evo_hw_graphics_print_info(evo_hw_graphics_context_t *hw_ctx, const char *prefix);

/**
 * Get performance statistics.
 * 
 * @param hw_ctx Hardware graphics context
 * @param frames_out Pointer to store frame count
 * @param hw_calls_out Pointer to store hardware render calls
 * @param sw_calls_out Pointer to store software render calls
 */
void evo_hw_graphics_get_stats(evo_hw_graphics_context_t *hw_ctx,
                              unsigned long *frames_out,
                              unsigned long *hw_calls_out, 
                              unsigned long *sw_calls_out);

/* ============================================================================
 * EGL DISPLAY DETECTION AND SELECTION
 * ============================================================================ */

/**
 * Detect available EGL displays on the EVO system.
 * Returns information about PowerVR SGX540 and Vivante GC320 availability.
 * 
 * @param displays_out Array to store display information (max 4 entries)
 * @param max_displays Maximum number of displays to check
 * @return Number of available EGL displays found
 */
int evo_hw_graphics_detect_displays(int *displays_out, int max_displays);

/**
 * Get EGL display information for debugging.
 * 
 * @param egl_display_id EGL display ID to query (1 or 2)
 * @param info_buffer Buffer to store information string
 * @param buffer_size Size of information buffer
 * @return 0 on success, -1 on failure
 */
int evo_hw_graphics_get_display_info(int egl_display_id, char *info_buffer, int buffer_size);

/* ============================================================================
 * OPENGL ES CONVENIENCE FUNCTIONS
 * ============================================================================ */

/**
 * Clear the hardware framebuffer with a solid color.
 * 
 * @param hw_ctx Hardware graphics context
 * @param red Red component (0.0-1.0)
 * @param green Green component (0.0-1.0)  
 * @param blue Blue component (0.0-1.0)
 * @param alpha Alpha component (0.0-1.0)
 */
void evo_hw_graphics_clear(evo_hw_graphics_context_t *hw_ctx, 
                          float red, float green, float blue, float alpha);

/**
 * Set the OpenGL ES viewport.
 * 
 * @param hw_ctx Hardware graphics context
 * @param x Viewport X offset
 * @param y Viewport Y offset
 * @param width Viewport width
 * @param height Viewport height
 */
void evo_hw_graphics_set_viewport(evo_hw_graphics_context_t *hw_ctx,
                                 int x, int y, int width, int height);

/**
 * Render a simple colored triangle using OpenGL ES 1.1 immediate mode.
 * 
 * @param hw_ctx Hardware graphics context
 * @param angle Rotation angle in degrees
 * @param red Red color component (0.0-1.0)
 * @param green Green color component (0.0-1.0)
 * @param blue Blue color component (0.0-1.0)
 */
void evo_hw_graphics_render_triangle(evo_hw_graphics_context_t *hw_ctx,
                                    float angle, float red, float green, float blue);

#ifdef __cplusplus
}
#endif

#endif /* EVO_HW_GRAPHICS_H */

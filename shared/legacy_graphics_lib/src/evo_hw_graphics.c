/*
 * EVO Hardware Graphics - EGL/OpenGL ES Implementation
 * 
 * This implementation provides hardware-accelerated graphics support for EVO
 * embedded systems using EGL and OpenGL ES with PowerVR SGX540 and Vivante GC320.
 * 
 * Target Configuration (from graphics-1440_540.conf):
 * - EGL Display 1: PowerVR SGX540 (libIMGegl.so, libImgGLESv2.so)
 * - EGL Display 2: Vivante GC320 (libGalcore.so)
 * 
 * Build Requirements:
 * - Target libraries: /toolchain/target/lib and /toolchain/target/opt/sys/lib
 * - EGL libraries: libEGL_X.cpp-ne.so, libGLESv2_X_FORWARD.cpp-ne.so
 * - PowerVR libraries: libIMGegl.so, libImgGLESv2.so, libImgGLESv1_CM.so
 */

#include "evo_hw_graphics.h"
#include "evo_util.h"
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

/* ============================================================================
 * POWERVR SGX540 SLOT MANAGEMENT
 * ============================================================================ */

/**
 * Clean up PowerVR display slots by terminating conflicting processes
 * Based on decompiled source analysis: PowerVR supports max 10 concurrent EGL contexts
 */
static void powervr_cleanup_slots(void) {
    printf("🧹 PowerVR Slot Cleanup: Ensuring available display slots...\n");
    
    /* Kill known PowerVR-using processes that might hold slots */
    const char* cleanup_commands[] = {
        "killall -9 Screen 2>/dev/null || true",
        "killall -9 screen-gles 2>/dev/null || true", 
        "killall -9 hw_graphics_demo 2>/dev/null || true",
        "killall -9 simple_graphics_demo 2>/dev/null || true",
        NULL
    };
    
    for (int i = 0; cleanup_commands[i] != NULL; i++) {
        int ret = system(cleanup_commands[i]);
        if (WEXITSTATUS(ret) == 0) {
            printf("🧹 Cleanup: %s\n", cleanup_commands[i]);
        }
    }
    
    /* Brief delay to allow PowerVR driver to release slots */
    usleep(500000); /* 500ms */
    printf("✅ PowerVR slot cleanup completed\n");
}

/* ============================================================================
 * DYNAMIC LIBRARY LOADING FOR EGL/OPENGL ES
 * ============================================================================ */

/* Function pointers for dynamically loaded EGL functions */
static EGLDisplay (*dyn_eglGetDisplay)(EGLNativeDisplayType) = NULL;
static EGLBoolean (*dyn_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = NULL;
static EGLBoolean (*dyn_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = NULL;
static EGLContext (*dyn_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = NULL;
static EGLSurface (*dyn_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = NULL;
static EGLBoolean (*dyn_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = NULL;
static EGLBoolean (*dyn_eglSwapBuffers)(EGLDisplay, EGLSurface) = NULL;
static EGLBoolean (*dyn_eglDestroySurface)(EGLDisplay, EGLSurface) = NULL;
static EGLBoolean (*dyn_eglDestroyContext)(EGLDisplay, EGLContext) = NULL;
static EGLBoolean (*dyn_eglTerminate)(EGLDisplay) = NULL;
static EGLint (*dyn_eglGetError)(void) = NULL;
static EGLBoolean (*dyn_eglBindAPI)(EGLenum) = NULL;

/* Function pointers for dynamically loaded OpenGL ES functions */
static const GLubyte* (*dyn_glGetString)(GLenum) = NULL;
static void (*dyn_glViewport)(GLint, GLint, GLsizei, GLsizei) = NULL;
static void (*dyn_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = NULL;
static void (*dyn_glClear)(GLbitfield) = NULL;
static GLenum (*dyn_glGetError)(void) = NULL;

/* Additional OpenGL ES 2.0 functions for shader-based triangle rendering (PowerVR SGX540) */
static GLuint (*dyn_glCreateShader)(GLenum) = NULL;
static void (*dyn_glShaderSource)(GLuint, GLsizei, const char**, const GLint*) = NULL;
static void (*dyn_glCompileShader)(GLuint) = NULL;
static GLuint (*dyn_glCreateProgram)(void) = NULL;
static void (*dyn_glAttachShader)(GLuint, GLuint) = NULL;
static void (*dyn_glLinkProgram)(GLuint) = NULL;
static void (*dyn_glUseProgram)(GLuint) = NULL;
static GLint (*dyn_glGetAttribLocation)(GLuint, const char*) = NULL;
static GLint (*dyn_glGetUniformLocation)(GLuint, const char*) = NULL;
static void (*dyn_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = NULL;
static void (*dyn_glEnableVertexAttribArray)(GLuint) = NULL;
static void (*dyn_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = NULL;
static void (*dyn_glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*) = NULL;
static void (*dyn_glDrawArrays)(GLenum, GLint, GLsizei) = NULL;
static void (*dyn_glFlush)(void) = NULL;
static void (*dyn_glDeleteShader)(GLuint) = NULL;
static void (*dyn_glDeleteProgram)(GLuint) = NULL;
static void (*dyn_glGetShaderiv)(GLuint, GLenum, GLint*) = NULL;
static void (*dyn_glGetProgramiv)(GLuint, GLenum, GLint*) = NULL;
static void (*dyn_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*) = NULL;
static void (*dyn_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*) = NULL;

/* Library handles for dynamic loading */
static void *egl_handle = NULL;
static void *gles_handle = NULL;
static int libraries_loaded = 0;

/**
 * Load EGL and OpenGL ES libraries dynamically
 */
static int evo_hw_load_libraries(void) {
    if (libraries_loaded) {
        printf("EVO HW Graphics: Libraries already loaded\n");
        return 0; /* Already loaded */
    }
    
    printf("EVO HW Graphics: Loading PowerVR SGX540 libraries...\n");
    
    /* Load PowerVR SGX540 EGL library (target-specific from graphics config) */
    printf("EVO HW Graphics: Trying libIMGegl.so (PowerVR target-specific)...\n");
    egl_handle = dlopen("libIMGegl.so", RTLD_LAZY);
    if (!egl_handle) {
        printf("EVO HW Graphics: libIMGegl.so failed: %s\n", dlerror());
        /* Try with full path to graphics directory */
        printf("EVO HW Graphics: Trying /fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so...\n");
        egl_handle = dlopen("/fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so", RTLD_LAZY);
        if (!egl_handle) {
            printf("EVO HW Graphics: Full path libIMGegl.so failed: %s\n", dlerror());
            /* Fallback to EVO embedded optimized */
            printf("EVO HW Graphics: Trying libEGL_X.cpp-ne.so (EVO embedded)...\n");
            egl_handle = dlopen("libEGL_X.cpp-ne.so", RTLD_LAZY);
            if (!egl_handle) {
                printf("EVO HW Graphics: libEGL_X.cpp-ne.so failed: %s\n", dlerror());
                /* Final fallback to standard system EGL */
                printf("EVO HW Graphics: Trying libEGL.so.1 (system fallback)...\n");
                egl_handle = dlopen("libEGL.so.1", RTLD_LAZY);
                if (!egl_handle) {
                    printf("EVO HW Graphics: All EGL libraries failed: %s\n", dlerror());
                    return -1;
                } else {
                    printf("EVO HW Graphics: ✓ Loaded libEGL.so.1 (system fallback)\n");
                }
            } else {
                printf("EVO HW Graphics: ✓ Loaded libEGL_X.cpp-ne.so (EVO embedded)\n");
            }
        } else {
            printf("EVO HW Graphics: ✓ Loaded /fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so (PowerVR full path)\n");
        }
    } else {
        printf("EVO HW Graphics: ✓ Loaded libIMGegl.so (PowerVR target-specific)\n");
    }
    
    /* Load PowerVR SGX540 OpenGL ES 2.0 library (target-specific) */
    printf("EVO HW Graphics: Trying libImgGLESv2.so (PowerVR target-specific)...\n");
    gles_handle = dlopen("libImgGLESv2.so", RTLD_LAZY);
    if (!gles_handle) {
        printf("EVO HW Graphics: libImgGLESv2.so failed: %s\n", dlerror());
        /* Try with full path to graphics directory */
        printf("EVO HW Graphics: Trying /fs/sda0/lib/graphics/omap5430_ES2_0/libImgGLESv2.so...\n");
        gles_handle = dlopen("/fs/sda0/lib/graphics/omap5430_ES2_0/libImgGLESv2.so", RTLD_LAZY);
        if (!gles_handle) {
            printf("EVO HW Graphics: Full path libImgGLESv2.so failed: %s\n", dlerror());
            /* Fallback to EVO embedded */
            printf("EVO HW Graphics: Trying libGLESv2_X_FORWARD.cpp-ne.so (EVO embedded)...\n");
            gles_handle = dlopen("libGLESv2_X_FORWARD.cpp-ne.so", RTLD_LAZY);
            if (!gles_handle) {
                printf("EVO HW Graphics: libGLESv2_X_FORWARD.cpp-ne.so failed: %s\n", dlerror());
                /* Final fallback to standard system OpenGL ES */
                printf("EVO HW Graphics: Trying libGLESv2.so.2 (system fallback)...\n");
                gles_handle = dlopen("libGLESv2.so.2", RTLD_LAZY);
                if (!gles_handle) {
                    printf("EVO HW Graphics: All OpenGL ES libraries failed: %s\n", dlerror());
                    dlclose(egl_handle);
                    egl_handle = NULL;
                    return -1;
                } else {
                    printf("EVO HW Graphics: ✓ Loaded libGLESv2.so.2 (system fallback)\n");
                }
            } else {
                printf("EVO HW Graphics: ✓ Loaded libGLESv2_X_FORWARD.cpp-ne.so (EVO embedded)\n");
            }
        } else {
            printf("EVO HW Graphics: ✓ Loaded /fs/sda0/lib/graphics/omap5430_ES2_0/libImgGLESv2.so (PowerVR full path)\n");
        }
    } else {
        printf("EVO HW Graphics: ✓ Loaded libImgGLESv2.so (PowerVR target-specific)\n");
    }
    
    /* Load EGL function pointers (PowerVR IMG-prefixed functions) */
    dyn_eglGetDisplay = dlsym(egl_handle, "IMGeglGetDisplay");
    dyn_eglInitialize = dlsym(egl_handle, "IMGeglInitialize");
    dyn_eglChooseConfig = dlsym(egl_handle, "IMGeglChooseConfig");
    dyn_eglCreateContext = dlsym(egl_handle, "IMGeglCreateContext");
    dyn_eglCreateWindowSurface = dlsym(egl_handle, "IMGeglCreateWindowSurface");
    dyn_eglMakeCurrent = dlsym(egl_handle, "IMGeglMakeCurrent");
    dyn_eglSwapBuffers = dlsym(egl_handle, "IMGeglSwapBuffers");
    dyn_eglDestroySurface = dlsym(egl_handle, "IMGeglDestroySurface");
    dyn_eglDestroyContext = dlsym(egl_handle, "IMGeglDestroyContext");
    dyn_eglTerminate = dlsym(egl_handle, "IMGeglTerminate");
    dyn_eglGetError = dlsym(egl_handle, "IMGeglGetError");
    dyn_eglBindAPI = dlsym(egl_handle, "IMGeglBindAPI");
    
    /* Load OpenGL ES function pointers */
    dyn_glGetString = dlsym(gles_handle, "glGetString");
    dyn_glViewport = dlsym(gles_handle, "glViewport");
    dyn_glClearColor = dlsym(gles_handle, "glClearColor");
    dyn_glClear = dlsym(gles_handle, "glClear");
    dyn_glGetError = dlsym(gles_handle, "glGetError");
    
    /* Load OpenGL ES 2.0 shader system functions */
    dyn_glCreateShader = dlsym(gles_handle, "glCreateShader");
    dyn_glShaderSource = dlsym(gles_handle, "glShaderSource");
    dyn_glCompileShader = dlsym(gles_handle, "glCompileShader");
    dyn_glCreateProgram = dlsym(gles_handle, "glCreateProgram");
    dyn_glAttachShader = dlsym(gles_handle, "glAttachShader");
    dyn_glLinkProgram = dlsym(gles_handle, "glLinkProgram");
    dyn_glUseProgram = dlsym(gles_handle, "glUseProgram");
    dyn_glGetAttribLocation = dlsym(gles_handle, "glGetAttribLocation");
    dyn_glGetUniformLocation = dlsym(gles_handle, "glGetUniformLocation");
    dyn_glVertexAttribPointer = dlsym(gles_handle, "glVertexAttribPointer");
    dyn_glEnableVertexAttribArray = dlsym(gles_handle, "glEnableVertexAttribArray");
    dyn_glUniform4f = dlsym(gles_handle, "glUniform4f");
    dyn_glUniformMatrix3fv = dlsym(gles_handle, "glUniformMatrix3fv");
    dyn_glDrawArrays = dlsym(gles_handle, "glDrawArrays");
    dyn_glFlush = dlsym(gles_handle, "glFlush");
    dyn_glDeleteShader = dlsym(gles_handle, "glDeleteShader");
    dyn_glDeleteProgram = dlsym(gles_handle, "glDeleteProgram");
    dyn_glGetShaderiv = dlsym(gles_handle, "glGetShaderiv");
    dyn_glGetProgramiv = dlsym(gles_handle, "glGetProgramiv");
    dyn_glGetShaderInfoLog = dlsym(gles_handle, "glGetShaderInfoLog");
    dyn_glGetProgramInfoLog = dlsym(gles_handle, "glGetProgramInfoLog");
    
    /* Check that all required functions were loaded */
    if (!dyn_eglGetDisplay || !dyn_eglInitialize || !dyn_glGetString) {
        printf("EVO HW Graphics: Failed to load required function pointers\n");
        dlclose(egl_handle);
        dlclose(gles_handle);
        egl_handle = NULL;
        gles_handle = NULL;
        return -1;
    }
    
    libraries_loaded = 1;
    printf("EVO HW Graphics: Successfully loaded EGL and OpenGL ES libraries\n");
    return 0;
}

/**
 * Unload dynamically loaded libraries
 */
static void evo_hw_unload_libraries(void) {
    if (egl_handle) {
        dlclose(egl_handle);
        egl_handle = NULL;
    }
    if (gles_handle) {
        dlclose(gles_handle);
        gles_handle = NULL;
    }
    libraries_loaded = 0;
}

/* ============================================================================
 * INTERNAL DEFINITIONS
 * ============================================================================ */

/* EGL display IDs for EVO target system */
#define EVO_EGL_DISPLAY_POWERVR   1     /* PowerVR SGX540 (main display) */
#define EVO_EGL_DISPLAY_VIVANTE   2     /* Vivante GC320 (secondary/video) */

/* Default configuration values */
#define EVO_HW_DEFAULT_SAMPLES          4
#define EVO_HW_DEFAULT_DEPTH_BITS      24
#define EVO_HW_DEFAULT_STENCIL_BITS     8

/* Error message buffer size */
#define EVO_HW_ERROR_BUFFER_SIZE      256

/* ============================================================================
 * INTERNAL UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Set error message in hardware graphics context
 */
static void evo_hw_set_error(evo_hw_graphics_context_t *hw_ctx, const char *format, ...) {
    if (!hw_ctx) return;
    
    va_list args;
    va_start(args, format);
    vsnprintf(hw_ctx->error_message, sizeof(hw_ctx->error_message), format, args);
    va_end(args);
}

/**
 * Get EGL native display for the specified display ID
 */
static EGLNativeDisplayType evo_hw_get_native_display(int egl_display_id) {
    /* For QNX/EVO, EGL native display is typically just the display ID */
    return (EGLNativeDisplayType)(intptr_t)egl_display_id;
}

/**
 * Initialize EGL display with fallback logic
 */
static int evo_hw_init_egl_display(evo_hw_graphics_context_t *hw_ctx) {
    EGLint major, minor;
    
    /* Load libraries first */
    if (evo_hw_load_libraries() != 0) {
        evo_hw_set_error(hw_ctx, "Failed to load EGL/OpenGL ES libraries");
        return -1;
    }
    
    /* Try to get EGL display for the configured display ID */
    EGLNativeDisplayType native_display = evo_hw_get_native_display(hw_ctx->config.egl_display_id);
    hw_ctx->egl_display = dyn_eglGetDisplay(native_display);
    
    if (hw_ctx->egl_display == EGL_NO_DISPLAY) {
        /* Fallback: try default display */
        hw_ctx->egl_display = dyn_eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (hw_ctx->egl_display == EGL_NO_DISPLAY) {
            evo_hw_set_error(hw_ctx, "Failed to get EGL display");
            return -1;
        }
    }
    
    /* Initialize EGL */
    if (!dyn_eglInitialize(hw_ctx->egl_display, &major, &minor)) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to initialize EGL (error: 0x%x)", hw_ctx->last_egl_error);
        return -1;
    }
    
    printf("EVO HW Graphics: EGL %d.%d initialized on display %d\n", 
           major, minor, hw_ctx->config.egl_display_id);
    
    return 0;
}

/**
 * Choose optimal EGL configuration
 */
static int evo_hw_choose_egl_config(evo_hw_graphics_context_t *hw_ctx) {
    EGLint config_count;
    EGLConfig configs[16];
    
    /* Build configuration attributes */
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, (hw_ctx->config.prefer_gles_version == 2) ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, hw_ctx->config.depth_buffer_bits,
        EGL_STENCIL_SIZE, hw_ctx->config.stencil_buffer_bits,
        EGL_SAMPLES, (hw_ctx->config.enable_multisampling) ? hw_ctx->config.samples : 0,
        EGL_NONE
    };
    
    /* Get available configurations */
    if (!dyn_eglChooseConfig(hw_ctx->egl_display, config_attribs, configs, 16, &config_count)) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to choose EGL config (error: 0x%x)", hw_ctx->last_egl_error);
        return -1;
    }
    
    if (config_count == 0) {
        /* Try with reduced requirements */
        EGLint fallback_attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
            EGL_RED_SIZE, 5,
            EGL_GREEN_SIZE, 6,
            EGL_BLUE_SIZE, 5,
            EGL_NONE
        };
        
        if (!dyn_eglChooseConfig(hw_ctx->egl_display, fallback_attribs, configs, 16, &config_count)) {
            hw_ctx->last_egl_error = dyn_eglGetError();
            evo_hw_set_error(hw_ctx, "Failed to find any suitable EGL config");
            return -1;
        }
    }
    
    if (config_count == 0) {
        evo_hw_set_error(hw_ctx, "No suitable EGL configurations found");
        return -1;
    }
    
    /* Use the first available configuration */
    hw_ctx->egl_config = configs[0];
    
    printf("EVO HW Graphics: Selected EGL config (found %d options)\n", config_count);
    return 0;
}

/* PowerVR slot management and timeout handling */
static volatile int context_creation_timeout = 0;
static void timeout_handler(int sig) {
    context_creation_timeout = 1;
    printf("⚠️ PowerVR context creation timeout - likely slot exhaustion\n");
}

/**
 * Create EGL context for OpenGL ES with enhanced PowerVR slot management
 */
static int evo_hw_create_egl_context(evo_hw_graphics_context_t *hw_ctx) {
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, hw_ctx->config.prefer_gles_version,
        EGL_NONE
    };
    
    /* Bind OpenGL ES API */
    if (!dyn_eglBindAPI(EGL_OPENGL_ES_API)) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to bind OpenGL ES API (error: 0x%x)", hw_ctx->last_egl_error);
        return -1;
    }
    
    printf("EVO HW Graphics: Creating EGL context with PowerVR slot management...\n");
    
    /* **ENHANCED POWERVR FIX**: Aggressive timeout with signal handling */
    struct sigaction sa, old_sa;
    context_creation_timeout = 0;
    
    /* Set up aggressive timeout signal handler */
    sa.sa_handler = timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* No flags for QNX compatibility */
    sigaction(SIGALRM, &sa, &old_sa);
    
    /* Set aggressive 10-second timeout for PowerVR context creation */
    printf("EVO HW Graphics: Setting 10-second timeout for PowerVR slot acquisition...\n");
    alarm(10);
    
    /* Attempt context creation with timeout protection */
    hw_ctx->egl_context = dyn_eglCreateContext(hw_ctx->egl_display, hw_ctx->egl_config, 
                                          EGL_NO_CONTEXT, context_attribs);
    
    /* Cancel timeout and restore signal handler */
    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);
    
    /* Check for timeout condition */
    if (context_creation_timeout) {
        evo_hw_set_error(hw_ctx, "PowerVR context creation timed out - display slots exhausted");
        printf("🚨 PowerVR SGX540 display slots are exhausted (max 10 slots)\n");
        printf("💡 Solution: Clean up existing PowerVR processes\n");
        printf("💡 Commands: killall -9 Screen; killall -9 screen-gles; killall -9 hw_graphics_demo\n");
        return -1;
    }
    
    if (hw_ctx->egl_context == EGL_NO_CONTEXT) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        
        /* Enhanced PowerVR error detection based on decompiled source analysis */
        if (hw_ctx->last_egl_error == 0x3000) { /* EGL_SUCCESS but context is NULL */
            evo_hw_set_error(hw_ctx, "PowerVR slot exhaustion detected (IMGeglCreateContext returned NULL)");
            printf("🔍 PowerVR Analysis: All 10 display slots occupied\n");
            printf("� Function: IMGeglCreateContext at 0x00019e8c failed\n");
            printf("� Root Cause: PowerVR SGX540 has limited concurrent EGL contexts\n");
        } else {
            evo_hw_set_error(hw_ctx, "Failed to create EGL context (error: 0x%x)", hw_ctx->last_egl_error);
        }
        return -1;
    }
    
    printf("EVO HW Graphics: ✅ PowerVR EGL context created successfully\n");
    printf("EVO HW Graphics: OpenGL ES %d context active\n", hw_ctx->config.prefer_gles_version);
    return 0;
}

/**
 * Create EGL surface from Screen window
 */
static int evo_hw_create_egl_surface(evo_hw_graphics_context_t *hw_ctx) {
    if (!hw_ctx->display->screen_win) {
        evo_hw_set_error(hw_ctx, "No Screen window available for EGL surface");
        return -1;
    }
    
    /* Create EGL window surface from Screen window */
    hw_ctx->egl_surface = dyn_eglCreateWindowSurface(hw_ctx->egl_display, hw_ctx->egl_config,
                                                (EGLNativeWindowType)hw_ctx->display->screen_win, NULL);
    if (hw_ctx->egl_surface == EGL_NO_SURFACE) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to create EGL surface (error: 0x%x)", hw_ctx->last_egl_error);
        return -1;
    }
    
    printf("EVO HW Graphics: EGL surface created from Screen window\n");
    return 0;
}

/**
 * Detect OpenGL ES capabilities
 */
static void evo_hw_detect_capabilities(evo_hw_graphics_context_t *hw_ctx) {
    const char *version_string;
    const char *extensions_string;
    
    hw_ctx->capabilities = EVO_HW_GRAPHICS_NONE;
    
    /* Make context current for capability detection */
    if (!dyn_eglMakeCurrent(hw_ctx->egl_display, hw_ctx->egl_surface, 
                       hw_ctx->egl_surface, hw_ctx->egl_context)) {
        return;
    }
    
    /* Check OpenGL ES version */
    version_string = (const char*)dyn_glGetString(GL_VERSION);
    if (version_string) {
        printf("EVO HW Graphics: OpenGL ES Version: %s\n", version_string);
        
        if (strstr(version_string, "OpenGL ES 1.")) {
            hw_ctx->capabilities |= EVO_HW_GRAPHICS_GLES1;
        }
        if (strstr(version_string, "OpenGL ES 2.") || strstr(version_string, "OpenGL ES 3.")) {
            hw_ctx->capabilities |= EVO_HW_GRAPHICS_GLES2;
        }
    }
    
    /* Check extensions */
    extensions_string = (const char*)dyn_glGetString(GL_EXTENSIONS);
    if (extensions_string) {
        printf("EVO HW Graphics: Extensions available\n");
    }
    
    /* Set actual GLES version based on what was created */
    hw_ctx->gles_version = hw_ctx->config.prefer_gles_version;
    
    /* Set viewport to match display dimensions */
    hw_ctx->viewport_width = hw_ctx->display->width;
    hw_ctx->viewport_height = hw_ctx->display->height;
    dyn_glViewport(0, 0, hw_ctx->viewport_width, hw_ctx->viewport_height);
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

evo_hw_graphics_config_t evo_hw_graphics_default_config(void) {
    evo_hw_graphics_config_t config;
    memset(&config, 0, sizeof(config));
    
    /* Use PowerVR SGX540 (EGL display 1) by default for main 1440x540 display */
    config.egl_display_id = EVO_EGL_DISPLAY_POWERVR;
    config.prefer_gles_version = 2;  /* OpenGL ES 2.0 preferred */
    config.enable_vsync = 1;
    config.enable_multisampling = 1;
    config.samples = EVO_HW_DEFAULT_SAMPLES;
    config.depth_buffer_bits = EVO_HW_DEFAULT_DEPTH_BITS;
    config.stencil_buffer_bits = EVO_HW_DEFAULT_STENCIL_BITS;
    
    return config;
}

evo_hw_graphics_context_t* evo_hw_graphics_init(evo_display_context_t *display_ctx,
                                                const evo_hw_graphics_config_t *config) {
    evo_hw_graphics_context_t *hw_ctx;
    
    if (!display_ctx) {
        printf("EVO HW Graphics: Invalid display context\n");
        return NULL;
    }
    
    /* Load EGL/OpenGL ES libraries dynamically */
    if (evo_hw_load_libraries() != 0) {
        printf("EVO HW Graphics: Failed to load EGL/OpenGL ES libraries\n");
        return NULL;
    }
    
    /* Allocate hardware graphics context */
    hw_ctx = calloc(1, sizeof(evo_hw_graphics_context_t));
    if (!hw_ctx) {
        printf("EVO HW Graphics: Memory allocation failed\n");
        return NULL;
    }
    
    /* Initialize basic state */
    hw_ctx->display = display_ctx;
    hw_ctx->egl_display = EGL_NO_DISPLAY;
    hw_ctx->egl_context = EGL_NO_CONTEXT;
    hw_ctx->egl_surface = EGL_NO_SURFACE;
    hw_ctx->render_mode = EVO_HW_RENDER_HARDWARE_ONLY;
    
    /* Use provided config or default */
    if (config) {
        hw_ctx->config = *config;
    } else {
        hw_ctx->config = evo_hw_graphics_default_config();
    }
    
    /* Store EGL display ID for reference */
    hw_ctx->egl_display_id = hw_ctx->config.egl_display_id;
    
    printf("EVO HW Graphics: Initializing with EGL display %d (PowerVR SGX540)\n", 
           hw_ctx->egl_display_id);
    
    /* Initialize EGL display */
    if (evo_hw_init_egl_display(hw_ctx) != 0) {
        printf("EVO HW Graphics: EGL display initialization failed\n");
        evo_hw_graphics_destroy(&hw_ctx);
        return NULL;
    }
    
    /* Choose EGL configuration */
    if (evo_hw_choose_egl_config(hw_ctx) != 0) {
        printf("EVO HW Graphics: EGL config selection failed\n");
        evo_hw_graphics_destroy(&hw_ctx);
        return NULL;
    }
    
    /* **POWERVR SLOT MANAGEMENT**: Clean up slots before context creation */
    powervr_cleanup_slots();
    
    /* Create EGL context */
    if (evo_hw_create_egl_context(hw_ctx) != 0) {
        printf("EVO HW Graphics: EGL context creation failed\n");
        evo_hw_graphics_destroy(&hw_ctx);
        return NULL;
    }
    
    /* Create EGL surface */
    if (evo_hw_create_egl_surface(hw_ctx) != 0) {
        printf("EVO HW Graphics: EGL surface creation failed\n");
        evo_hw_graphics_destroy(&hw_ctx);
        return NULL;
    }
    
    /* Detect capabilities */
    evo_hw_detect_capabilities(hw_ctx);
    
    if (hw_ctx->capabilities == EVO_HW_GRAPHICS_NONE) {
        printf("EVO HW Graphics: No OpenGL ES capabilities detected\n");
        evo_hw_graphics_destroy(&hw_ctx);
        return NULL;
    }
    
    printf("EVO HW Graphics: Successfully initialized with capabilities 0x%x\n", 
           hw_ctx->capabilities);
    
    return hw_ctx;
}

int evo_hw_graphics_begin_frame(evo_hw_graphics_context_t *hw_ctx) {
    if (!hw_ctx || hw_ctx->render_mode == EVO_HW_RENDER_SOFTWARE_ONLY) {
        return -1;
    }
    
    /* Make EGL context current */
    if (!dyn_eglMakeCurrent(hw_ctx->egl_display, hw_ctx->egl_surface,
                       hw_ctx->egl_surface, hw_ctx->egl_context)) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to make EGL context current (error: 0x%x)", 
                        hw_ctx->last_egl_error);
        return -1;
    }
    
    /* Set viewport */
    dyn_glViewport(0, 0, hw_ctx->viewport_width, hw_ctx->viewport_height);
    
    /* Check for OpenGL errors */
    hw_ctx->last_gl_error = dyn_glGetError();
    if (hw_ctx->last_gl_error != GL_NO_ERROR) {
        evo_hw_set_error(hw_ctx, "OpenGL error in begin_frame: 0x%x", hw_ctx->last_gl_error);
        return -1;
    }
    
    hw_ctx->hw_render_calls++;
    return 0;
}

int evo_hw_graphics_present_frame(evo_hw_graphics_context_t *hw_ctx) {
    if (!hw_ctx || hw_ctx->render_mode == EVO_HW_RENDER_SOFTWARE_ONLY) {
        return -1;
    }
    
    /* Swap EGL buffers */
    if (!dyn_eglSwapBuffers(hw_ctx->egl_display, hw_ctx->egl_surface)) {
        hw_ctx->last_egl_error = dyn_eglGetError();
        evo_hw_set_error(hw_ctx, "Failed to swap EGL buffers (error: 0x%x)", 
                        hw_ctx->last_egl_error);
        return -1;
    }
    
    /* Update display context */
    if (evo_display_update(hw_ctx->display) != 0) {
        evo_hw_set_error(hw_ctx, "Failed to update display");
        return -1;
    }
    
    hw_ctx->frames_rendered++;
    return 0;
}

/* OpenGL ES 2.0 shader-based triangle rendering for PowerVR SGX540 */
static GLuint triangle_program = 0;
static GLint position_attr = -1;
static GLint vertex_color_attr = -1;
static GLint transform_uniform = -1;

/* Clean up shader program */
static void cleanup_triangle_shader(void) {
    if (triangle_program != 0 && dyn_glDeleteProgram) {
        dyn_glDeleteProgram(triangle_program);
        triangle_program = 0;
        position_attr = -1;
        vertex_color_attr = -1;
        transform_uniform = -1;
        printf("EVO HW Graphics: Shader program cleaned up\n");
    }
}

void evo_hw_graphics_destroy(evo_hw_graphics_context_t **hw_ctx) {
    if (!hw_ctx || !*hw_ctx) {
        return;
    }
    
    evo_hw_graphics_context_t *ctx = *hw_ctx;
    
    /* Destroy EGL resources */
    if (ctx->egl_display != EGL_NO_DISPLAY && libraries_loaded) {
        dyn_eglMakeCurrent(ctx->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        /* Clean up shader program */
        cleanup_triangle_shader();
        
        if (ctx->egl_surface != EGL_NO_SURFACE) {
            dyn_eglDestroySurface(ctx->egl_display, ctx->egl_surface);
        }
        
        if (ctx->egl_context != EGL_NO_CONTEXT) {
            dyn_eglDestroyContext(ctx->egl_display, ctx->egl_context);
        }
        
        dyn_eglTerminate(ctx->egl_display);
    }
    
    /* Unload dynamic libraries */
    evo_hw_unload_libraries();
    
    printf("EVO HW Graphics: Destroyed (rendered %lu frames)\n", ctx->frames_rendered);
    
    /* Free context */
    free(ctx);
    *hw_ctx = NULL;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

int evo_hw_graphics_is_available(evo_hw_graphics_context_t *hw_ctx) {
    return (hw_ctx && hw_ctx->capabilities != EVO_HW_GRAPHICS_NONE) ? 1 : 0;
}

evo_hw_graphics_caps_t evo_hw_graphics_get_capabilities(evo_hw_graphics_context_t *hw_ctx) {
    return hw_ctx ? hw_ctx->capabilities : EVO_HW_GRAPHICS_NONE;
}

int evo_hw_graphics_set_render_mode(evo_hw_graphics_context_t *hw_ctx, 
                                   evo_hw_render_mode_t mode) {
    if (!hw_ctx) {
        return -1;
    }
    
    hw_ctx->render_mode = mode;
    return 0;
}

int evo_hw_graphics_get_egl_error(evo_hw_graphics_context_t *hw_ctx) {
    return hw_ctx ? hw_ctx->last_egl_error : 0;
}

int evo_hw_graphics_get_gl_error(evo_hw_graphics_context_t *hw_ctx) {
    return hw_ctx ? hw_ctx->last_gl_error : 0;
}

const char* evo_hw_graphics_get_error(evo_hw_graphics_context_t *hw_ctx) {
    return hw_ctx ? hw_ctx->error_message : "No context";
}

void evo_hw_graphics_print_info(evo_hw_graphics_context_t *hw_ctx, const char *prefix) {
    if (!hw_ctx) {
        printf("%s: No hardware graphics context\n", prefix ? prefix : "HW Graphics");
        return;
    }
    
    printf("%s: EGL Display %d, GLES %d, Caps 0x%x, Mode %d\n",
           prefix ? prefix : "HW Graphics",
           hw_ctx->egl_display_id,
           hw_ctx->gles_version,
           hw_ctx->capabilities,
           hw_ctx->render_mode);
    
    printf("%s: Viewport %dx%d, Frames %lu\n",
           prefix ? prefix : "HW Graphics",
           hw_ctx->viewport_width,
           hw_ctx->viewport_height,
           hw_ctx->frames_rendered);
}

void evo_hw_graphics_get_stats(evo_hw_graphics_context_t *hw_ctx,
                              unsigned long *frames_out,
                              unsigned long *hw_calls_out, 
                              unsigned long *sw_calls_out) {
    if (!hw_ctx) {
        if (frames_out) *frames_out = 0;
        if (hw_calls_out) *hw_calls_out = 0;
        if (sw_calls_out) *sw_calls_out = 0;
        return;
    }
    
    if (frames_out) *frames_out = hw_ctx->frames_rendered;
    if (hw_calls_out) *hw_calls_out = hw_ctx->hw_render_calls;
    if (sw_calls_out) *sw_calls_out = hw_ctx->sw_render_calls;
}

/* ============================================================================
 * EGL DISPLAY DETECTION
 * ============================================================================ */

int evo_hw_graphics_detect_displays(int *displays_out, int max_displays) {
    int found = 0;
    
    if (!displays_out || max_displays < 1) {
        return 0;
    }
    
    /* Based on graphics-1440_540.conf, we know EGL displays 1 and 2 exist */
    
    /* EGL Display 1: PowerVR SGX540 */
    if (found < max_displays) {
        displays_out[found] = EVO_EGL_DISPLAY_POWERVR;
        found++;
    }
    
    /* EGL Display 2: Vivante GC320 */
    if (found < max_displays) {
        displays_out[found] = EVO_EGL_DISPLAY_VIVANTE;
        found++;
    }
    
    return found;
}

int evo_hw_graphics_get_display_info(int egl_display_id, char *info_buffer, int buffer_size) {
    if (!info_buffer || buffer_size < 1) {
        return -1;
    }
    
    switch (egl_display_id) {
        case EVO_EGL_DISPLAY_POWERVR:
            snprintf(info_buffer, buffer_size, 
                    "EGL Display 1: PowerVR SGX540 (libIMGegl.so, libImgGLESv2.so)");
            break;
            
        case EVO_EGL_DISPLAY_VIVANTE:
            snprintf(info_buffer, buffer_size,
                    "EGL Display 2: Vivante GC320 (libGalcore.so)");
            break;
            
        default:
            snprintf(info_buffer, buffer_size, "Unknown EGL display %d", egl_display_id);
            return -1;
    }
    
    return 0;
}

/* ============================================================================
 * OPENGL ES CONVENIENCE FUNCTIONS
 * ============================================================================ */

void evo_hw_graphics_clear(evo_hw_graphics_context_t *hw_ctx, 
                          float red, float green, float blue, float alpha) {
    if (!hw_ctx || hw_ctx->render_mode == EVO_HW_RENDER_SOFTWARE_ONLY || !libraries_loaded) {
        return;
    }
    
    dyn_glClearColor(red, green, blue, alpha);
    dyn_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void evo_hw_graphics_set_viewport(evo_hw_graphics_context_t *hw_ctx,
                                 int x, int y, int width, int height) {
    if (!hw_ctx || hw_ctx->render_mode == EVO_HW_RENDER_SOFTWARE_ONLY || !libraries_loaded) {
        return;
    }
    
    dyn_glViewport(x, y, width, height);
    hw_ctx->viewport_width = width;
    hw_ctx->viewport_height = height;
}


/* Advanced vertex shader with transformation matrix and per-vertex colors */
static const char* vertex_shader_source = 
    "attribute vec2 position;\n"
    "attribute vec3 vertexColor;\n"
    "uniform mat3 transform;\n"
    "varying vec3 fragmentColor;\n"
    "void main() {\n"
    "    vec3 transformed = transform * vec3(position, 1.0);\n"
    "    gl_Position = vec4(transformed.xy, 0.0, 1.0);\n"
    "    fragmentColor = vertexColor;\n"
    "}\n";

/* Simple fragment shader with smooth color interpolation */
static const char* fragment_shader_source = 
    "precision mediump float;\n"
    "varying vec3 fragmentColor;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(fragmentColor, 1.0);\n"
    "}\n";

/* Initialize triangle shader program (called once per context) */
static int init_triangle_shader(void) {
    /* Always reinitialize shader program for new OpenGL context */
    if (triangle_program != 0) {
        printf("EVO HW Graphics: Resetting shader program for new context\n");
        triangle_program = 0;
        position_attr = -1;
        vertex_color_attr = -1;
        transform_uniform = -1;
    }
    
    if (!dyn_glCreateShader || !dyn_glCreateProgram) {
        printf("⚠️ OpenGL ES 2.0 shader functions not available\n");
        return -1;
    }
    
    /* Create and compile vertex shader */
    GLuint vertex_shader = dyn_glCreateShader(0x8B31); /* GL_VERTEX_SHADER */
    if (vertex_shader == 0) {
        printf("⚠️ Failed to create vertex shader\n");
        return -1;
    }
    
    dyn_glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    dyn_glCompileShader(vertex_shader);
    
    /* Check vertex shader compilation */
    if (dyn_glGetShaderiv) {
        GLint compiled = 0;
        dyn_glGetShaderiv(vertex_shader, 0x8B81, &compiled); /* GL_COMPILE_STATUS */
        if (!compiled) {
            printf("⚠️ Vertex shader compilation failed\n");
            if (dyn_glGetShaderInfoLog) {
                char log[512];
                dyn_glGetShaderInfoLog(vertex_shader, sizeof(log), NULL, log);
                printf("   Vertex shader error: %s\n", log);
            }
            dyn_glDeleteShader(vertex_shader);
            return -1;
        }
        printf("✅ Vertex shader compiled successfully\n");
    }
    
    /* Create and compile fragment shader */
    GLuint fragment_shader = dyn_glCreateShader(0x8B30); /* GL_FRAGMENT_SHADER */
    if (fragment_shader == 0) {
        printf("⚠️ Failed to create fragment shader\n");
        dyn_glDeleteShader(vertex_shader);
        return -1;
    }
    
    dyn_glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    dyn_glCompileShader(fragment_shader);
    
    /* Check fragment shader compilation */
    if (dyn_glGetShaderiv) {
        GLint compiled = 0;
        dyn_glGetShaderiv(fragment_shader, 0x8B81, &compiled); /* GL_COMPILE_STATUS */
        if (!compiled) {
            printf("⚠️ Fragment shader compilation failed\n");
            if (dyn_glGetShaderInfoLog) {
                char log[512];
                dyn_glGetShaderInfoLog(fragment_shader, sizeof(log), NULL, log);
                printf("   Fragment shader error: %s\n", log);
            }
            dyn_glDeleteShader(vertex_shader);
            dyn_glDeleteShader(fragment_shader);
            return -1;
        }
        printf("✅ Fragment shader compiled successfully\n");
    }
    
    /* Create shader program */
    triangle_program = dyn_glCreateProgram();
    if (triangle_program == 0) {
        printf("⚠️ Failed to create shader program\n");
        dyn_glDeleteShader(vertex_shader);
        dyn_glDeleteShader(fragment_shader);
        return -1;
    }
    
    dyn_glAttachShader(triangle_program, vertex_shader);
    dyn_glAttachShader(triangle_program, fragment_shader);
    dyn_glLinkProgram(triangle_program);
    
    /* Check program linking */
    if (dyn_glGetProgramiv) {
        GLint linked = 0;
        dyn_glGetProgramiv(triangle_program, 0x8B82, &linked); /* GL_LINK_STATUS */
        if (!linked) {
            printf("⚠️ Shader program linking failed\n");
            if (dyn_glGetProgramInfoLog) {
                char log[512];
                dyn_glGetProgramInfoLog(triangle_program, sizeof(log), NULL, log);
                printf("   Program link error: %s\n", log);
            }
            dyn_glDeleteShader(vertex_shader);
            dyn_glDeleteShader(fragment_shader);
            dyn_glDeleteProgram(triangle_program);
            triangle_program = 0;
            return -1;
        }
        printf("✅ Shader program linked successfully\n");
    }
    
    /* Get attribute and uniform locations */
    position_attr = dyn_glGetAttribLocation(triangle_program, "position");
    vertex_color_attr = dyn_glGetAttribLocation(triangle_program, "vertexColor");
    transform_uniform = dyn_glGetUniformLocation(triangle_program, "transform");
    
    /* Clean up shaders (they're now linked into the program) */
    dyn_glDeleteShader(vertex_shader);
    dyn_glDeleteShader(fragment_shader);
    
    printf("✅ OpenGL ES 2.0 advanced triangle shader initialized (program=%u, position=%d, vertexColor=%d, transform=%d)\n", 
           triangle_program, position_attr, vertex_color_attr, transform_uniform);
    
    return 0;
}

void evo_hw_graphics_render_triangle(evo_hw_graphics_context_t *hw_ctx,
                                    float angle, float red, float green, float blue) {
    if (!hw_ctx || !dyn_glUseProgram || !dyn_glDrawArrays) {
        printf("⚠️ OpenGL ES 2.0 functions not available\n");
        return;
    }
    
    /* Initialize shader program only once */
    if (triangle_program == 0) {
        if (init_triangle_shader() != 0) {
            printf("⚠️ Failed to initialize animated triangle shader\n");
            return;
        }
    }
    
    /* Calculate animated triangle geometry */
    float time = angle * 0.01f; /* Convert angle to time for smooth animation */
    
    /* Animated size and shape parameters - using simple math only */
    float base_size = 0.4f + 0.3f * sinf(time * 0.8f); /* Size pulses between 0.4 and 0.7 */
    float shape_factor = 0.7f + 0.3f * sinf(time * 1.2f); /* Shape morphing */
    float height_factor = 0.8f + 0.4f * sinf(time * 0.9f); /* Height variation */
    
    /* Rotation angle in radians - use volatile to prevent sincosf optimization */
    volatile float rotation = angle * 0.02f; /* Smooth rotation */
    float cos_rot = cosf(rotation);
    float sin_rot = sinf(rotation);
    
    /* Animated triangle vertices (morphing triangle) */
    GLfloat vertices[] = {
        /* Top vertex - animated height and slight horizontal sway */
        0.1f * sinf(time * 1.5f), base_size * height_factor,
        
        /* Bottom left - animated width and position */
        -base_size * shape_factor - 0.1f * sinf(time * 0.7f), -base_size * 0.8f,
        
        /* Bottom right - animated width and position */ 
        base_size * shape_factor + 0.1f * sinf(time * 1.1f), -base_size * 0.8f
    };
    
    /* Create 3x3 transformation matrix (rotation + optional scaling) */
    float scale = 0.8f + 0.4f * sinf(time * 0.6f); /* Breathing effect */
    GLfloat transform_matrix[9] = {
        cos_rot * scale, -sin_rot * scale, 0.0f,  /* First column */
        sin_rot * scale,  cos_rot * scale, 0.0f,  /* Second column */
        0.0f,             0.0f,            1.0f   /* Third column */
    };
    
    /* Calculate super-animated per-vertex colors - each tip gets unique color animation */
    /* Top vertex: Primary color with rainbow cycling */
    float top_red = 0.5f + 0.5f * sinf(time * 2.0f + red * 6.28f);
    float top_green = 0.5f + 0.5f * sinf(time * 2.3f + green * 6.28f);
    float top_blue = 0.5f + 0.5f * sinf(time * 1.8f + blue * 6.28f);
    
    /* Bottom left: Complementary colors with phase shift */
    float left_red = 0.5f + 0.5f * sinf(time * 1.7f + 2.0f);
    float left_green = 0.3f + 0.7f * sinf(time * 2.1f + 4.0f);
    float left_blue = 0.4f + 0.6f * sinf(time * 1.9f + 1.0f);
    
    /* Bottom right: Contrasting colors with different frequencies */
    float right_red = 0.6f + 0.4f * sinf(time * 2.5f + 3.14f);
    float right_green = 0.2f + 0.8f * sinf(time * 1.6f + 5.0f);
    float right_blue = 0.7f + 0.3f * sinf(time * 2.2f + 0.5f);
    
    /* Per-vertex colors (RGB for each vertex) */
    GLfloat vertex_colors[] = {
        top_red, top_green, top_blue,         /* Top vertex: Rainbow cycling */
        left_red, left_green, left_blue,      /* Bottom left: Phase-shifted */
        right_red, right_green, right_blue    /* Bottom right: Contrasting */
    };
    
    /* Use our animated shader program */
    dyn_glUseProgram(triangle_program);
    
    /* Set transformation matrix */
    if (transform_uniform >= 0 && dyn_glUniformMatrix3fv) {
        dyn_glUniformMatrix3fv(transform_uniform, 1, 0, transform_matrix); /* GL_FALSE = 0 */
    }
    
    /* Set vertex positions */
    if (position_attr >= 0) {
        dyn_glVertexAttribPointer(position_attr, 2, 0x1406, 0, 0, vertices); /* GL_FLOAT */
        dyn_glEnableVertexAttribArray(position_attr);
    }
    
    /* Set per-vertex colors */
    if (vertex_color_attr >= 0) {
        dyn_glVertexAttribPointer(vertex_color_attr, 3, 0x1406, 0, 0, vertex_colors); /* GL_FLOAT, RGB */
        dyn_glEnableVertexAttribArray(vertex_color_attr);
    }
    
    /* Draw the animated triangle */
    dyn_glDrawArrays(0x0004, 0, 3); /* GL_TRIANGLES, 3 vertices */
    
    /* Flush GPU commands to prevent hanging */
    if (dyn_glFlush) {
        dyn_glFlush();
    }
    
}
    

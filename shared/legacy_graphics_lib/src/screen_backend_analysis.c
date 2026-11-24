/*
 * QNX Screen Backend Analysis Framework
 * 
 * This tool systematically tests different screen backends to understand
 * their capabilities, limitations, and implementation requirements.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <signal.h>
#include <screen/screen.h>

/* Backend configuration structure */
typedef struct {
    const char *name;
    const char *description;
    const char *library_path;
    const char *expected_features;
    int requires_egl;
    int supports_double_buffering;
    int supports_hardware_accel;
} screen_backend_config_t;

/* Available screen backends for OMAP5430 */
static screen_backend_config_t g_backends[] = {
    {
        .name = "basic",
        .description = "Basic Screen API (libscreen.so.1 only)",
        .library_path = NULL,
        .expected_features = "Single buffering, basic window management",
        .requires_egl = 0,
        .supports_double_buffering = 0,
        .supports_hardware_accel = 0
    },
    {
        .name = "powervr-2d",
        .description = "PowerVR 2D Backend (screen-pvr2d.so)",
        .library_path = "/fs/sda0/lib/graphics/omap5430_ES2_0/screen-pvr2d.so",
        .expected_features = "Double buffering, PowerVR 2D acceleration",
        .requires_egl = 0,
        .supports_double_buffering = 1,
        .supports_hardware_accel = 1
    },
    {
        .name = "powervr-gles2",
        .description = "PowerVR OpenGL ES 2.0 Backend (screen-gles2.so)",
        .library_path = "/fs/sda0/lib/graphics/omap5430_ES2_0/screen-gles2.so",
        .expected_features = "OpenGL ES 2.0, Hardware acceleration, Double buffering",
        .requires_egl = 1,
        .supports_double_buffering = 1,
        .supports_hardware_accel = 1
    },
    {
        .name = "vivante",
        .description = "Vivante GC320 Backend (screen-vivante-omap5430.so)",
        .library_path = "/fs/sda0/lib/graphics/omap5430_ES2_0/screen-vivante-omap5430.so",
        .expected_features = "Vivante GC320 2D acceleration (secondary display)",
        .requires_egl = 0,
        .supports_double_buffering = 1,
        .supports_hardware_accel = 1
    },
    {
        .name = "software",
        .description = "Software Backend (screen-sw.so)",
        .library_path = "/fs/sda0/lib/graphics/omap5430_ES2_0/screen-sw.so",
        .expected_features = "Software rendering, CPU-based",
        .requires_egl = 0,
        .supports_double_buffering = 1,
        .supports_hardware_accel = 0
    }
};

static const int g_backend_count = sizeof(g_backends) / sizeof(g_backends[0]);

/* Test result tracking */
typedef struct {
    const char *backend_name;
    const char *test_name;
    int result;
    const char *details;
} backend_test_result_t;

static backend_test_result_t g_test_results[100];
static int g_result_count = 0;

/* Signal handling for crash detection */
static volatile int g_signal_caught = 0;
static int g_signal_number = 0;
static const char *g_current_test = "unknown";
static const char *g_current_backend = "unknown";

void signal_handler(int sig) {
    g_signal_caught = 1;
    g_signal_number = sig;
    printf("\n💥 SIGNAL %s (%d) caught during %s test with %s backend\n", 
           sig == SIGBUS ? "SIGBUS" : sig == SIGSEGV ? "SIGSEGV" : "UNKNOWN",
           sig, g_current_test, g_current_backend);
}

static void record_backend_test(const char *backend, const char *test, int result, const char *details) {
    if (g_result_count < 100) {
        g_test_results[g_result_count].backend_name = backend;
        g_test_results[g_result_count].test_name = test;
        g_test_results[g_result_count].result = result;
        g_test_results[g_result_count].details = details ? details : (result == 0 ? "SUCCESS" : "FAILED");
        g_result_count++;
    }
}

/* Test 1: Backend Library Availability */
int test_backend_library_availability(const screen_backend_config_t *backend) {
    printf("🔍 Testing library availability for %s backend...\n", backend->name);
    g_current_test = "library_availability";
    
    if (!backend->library_path) {
        record_backend_test(backend->name, "Library Load", 0, "N/A - uses base libscreen.so.1");
        printf("  ✅ Basic backend - no additional library needed\n");
        return 0;
    }
    
    void *handle = dlopen(backend->library_path, RTLD_LAZY);
    if (!handle) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "dlopen failed: %s", dlerror());
        record_backend_test(backend->name, "Library Load", -1, error_msg);
        printf("  ❌ Failed to load %s: %s\n", backend->library_path, dlerror());
        return -1;
    }
    
    dlclose(handle);
    record_backend_test(backend->name, "Library Load", 0, "Library loads successfully");
    printf("  ✅ Backend library loads successfully\n");
    return 0;
}

/* Test 2: Context and Window Creation */
int test_basic_window_ops(const screen_backend_config_t *backend, screen_context_t *ctx, screen_window_t *win) {
    printf("🔍 Testing basic window operations for %s backend...\n", backend->name);
    g_current_test = "basic_window_ops";
    
    /* Create context */
    int rc = screen_create_context(ctx, 0);
    if (rc != 0) {
        record_backend_test(backend->name, "Context Creation", -1, "screen_create_context failed");
        return -1;
    }
    record_backend_test(backend->name, "Context Creation", 0, NULL);
    
    /* Create window */
    rc = screen_create_window(win, *ctx);
    if (rc != 0) {
        record_backend_test(backend->name, "Window Creation", -1, "screen_create_window failed");
        return -1;
    }
    
    /* Set basic properties */
    int format = SCREEN_FORMAT_RGBA8888;
    rc = screen_set_window_property_iv(*win, SCREEN_PROPERTY_FORMAT, &format);
    
    int size[2] = {640, 480};
    screen_set_window_property_iv(*win, SCREEN_PROPERTY_SIZE, size);
    
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    screen_set_window_property_iv(*win, SCREEN_PROPERTY_USAGE, &usage);
    
    record_backend_test(backend->name, "Window Creation", 0, NULL);
    printf("  ✅ Window creation successful\n");
    return 0;
}

/* Test 3: Single Buffer Operations */
int test_single_buffer_ops(const screen_backend_config_t *backend, screen_window_t win) {
    printf("🔍 Testing single buffer operations for %s backend...\n", backend->name);
    g_current_test = "single_buffer_ops";
    
    /* Create single buffer */
    int rc = screen_create_window_buffers(win, 1);
    if (rc != 0) {
        record_backend_test(backend->name, "Single Buffer Creation", -1, "screen_create_window_buffers(1) failed");
        return -1;
    }
    record_backend_test(backend->name, "Single Buffer Creation", 0, NULL);
    
    /* Get buffer handle */
    screen_buffer_t *buffers = NULL;
    rc = screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc != 0 || !buffers || !buffers[0]) {
        record_backend_test(backend->name, "Buffer Handle Access", -1, "Cannot get buffer handle");
        return -1;
    }
    record_backend_test(backend->name, "Buffer Handle Access", 0, NULL);
    
    /* Test buffer property access - this is where basic backend fails */
    g_signal_caught = 0;
    void *buffer_ptr = NULL;
    rc = screen_get_buffer_property_pv(buffers[0], SCREEN_PROPERTY_POINTER, &buffer_ptr);
    
    if (g_signal_caught) {
        record_backend_test(backend->name, "Buffer Property Access", -1, "SIGBUS - backend limitation");
        printf("  ❌ Buffer property access failed with SIGBUS (backend limitation)\n");
        return -1;
    }
    
    if (rc != 0 || !buffer_ptr) {
        record_backend_test(backend->name, "Buffer Property Access", -1, "Property access failed");
        return -1;
    }
    
    record_backend_test(backend->name, "Buffer Property Access", 0, NULL);
    printf("  ✅ Buffer property access successful\n");
    return 0;
}

/* Test 4: Double Buffer Operations */
int test_double_buffer_ops(const screen_backend_config_t *backend, screen_context_t ctx) {
    printf("🔍 Testing double buffer operations for %s backend...\n", backend->name);
    g_current_test = "double_buffer_ops";
    
    if (!backend->supports_double_buffering) {
        record_backend_test(backend->name, "Double Buffer Support", -1, "Not expected to support double buffering");
        printf("  ⚠️  Backend not expected to support double buffering\n");
        return -1;
    }
    
    /* Create new window for double buffer test */
    screen_window_t test_win;
    int rc = screen_create_window(&test_win, ctx);
    if (rc != 0) {
        record_backend_test(backend->name, "Double Buffer Window", -1, "Window creation failed");
        return -1;
    }
    
    /* Set properties for double buffering */
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(test_win, SCREEN_PROPERTY_FORMAT, &format);
    
    int size[2] = {640, 480};
    screen_set_window_property_iv(test_win, SCREEN_PROPERTY_SIZE, size);
    
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ;
    screen_set_window_property_iv(test_win, SCREEN_PROPERTY_USAGE, &usage);
    
    /* Create double buffers */
    g_signal_caught = 0;
    rc = screen_create_window_buffers(test_win, 2);
    
    if (g_signal_caught) {
        record_backend_test(backend->name, "Double Buffer Creation", -1, "Signal during creation");
        screen_destroy_window(test_win);
        return -1;
    }
    
    if (rc != 0) {
        record_backend_test(backend->name, "Double Buffer Creation", -1, "screen_create_window_buffers(2) failed");
        screen_destroy_window(test_win);
        return -1;
    }
    
    /* Test accessing both buffers */
    screen_buffer_t *buffers = NULL;
    rc = screen_get_window_property_pv(test_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc == 0 && buffers && buffers[0] && buffers[1]) {
        /* Test accessing both buffer properties */
        void *ptr0 = NULL, *ptr1 = NULL;
        g_signal_caught = 0;
        
        int rc0 = screen_get_buffer_property_pv(buffers[0], SCREEN_PROPERTY_POINTER, &ptr0);
        if (!g_signal_caught) {
            int rc1 = screen_get_buffer_property_pv(buffers[1], SCREEN_PROPERTY_POINTER, &ptr1);
            
            if (!g_signal_caught && rc0 == 0 && rc1 == 0 && ptr0 && ptr1) {
                record_backend_test(backend->name, "Double Buffer Properties", 0, "Both buffers accessible");
                printf("  ✅ Double buffer operations successful\n");
                screen_destroy_window(test_win);
                return 0;
            }
        }
        
        if (g_signal_caught) {
            record_backend_test(backend->name, "Double Buffer Properties", -1, "Signal during property access");
        } else {
            record_backend_test(backend->name, "Double Buffer Properties", -1, "Property access failed");
        }
    } else {
        record_backend_test(backend->name, "Double Buffer Handles", -1, "Invalid buffer handles");
    }
    
    screen_destroy_window(test_win);
    printf("  ❌ Double buffer operations failed\n");
    return -1;
}

/* Test 5: Hardware Acceleration Detection */
int test_hardware_acceleration(const screen_backend_config_t *backend) {
    printf("🔍 Testing hardware acceleration detection for %s backend...\n", backend->name);
    g_current_test = "hardware_acceleration";
    
    if (!backend->supports_hardware_accel) {
        record_backend_test(backend->name, "Hardware Acceleration", -1, "Not expected to have hardware acceleration");
        printf("  ⚠️  Backend not expected to have hardware acceleration\n");
        return -1;
    }
    
    /* For PowerVR backends, check if EGL libraries are accessible */
    if (backend->requires_egl) {
        void *egl_handle = dlopen("/fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so", RTLD_LAZY);
        if (!egl_handle) {
            record_backend_test(backend->name, "EGL Library Access", -1, "libIMGegl.so not accessible");
            return -1;
        }
        dlclose(egl_handle);
        record_backend_test(backend->name, "EGL Library Access", 0, NULL);
    }
    
    record_backend_test(backend->name, "Hardware Acceleration", 0, "Libraries accessible");
    printf("  ✅ Hardware acceleration components accessible\n");
    return 0;
}

/* Run comprehensive test suite for a single backend */
void test_backend_comprehensive(const screen_backend_config_t *backend) {
    printf("\n🚀 TESTING BACKEND: %s\n", backend->name);
    printf("Description: %s\n", backend->description);
    printf("Expected Features: %s\n", backend->expected_features);
    printf("========================================\n");
    
    g_current_backend = backend->name;
    
    /* Test 1: Library availability */
    if (test_backend_library_availability(backend) != 0) {
        printf("❌ Backend %s: Library not available, skipping further tests\n", backend->name);
        return;
    }
    
    /* Test 2: Basic operations */
    screen_context_t ctx = NULL;
    screen_window_t win = NULL;
    
    if (test_basic_window_ops(backend, &ctx, &win) != 0) {
        printf("❌ Backend %s: Basic operations failed\n", backend->name);
        goto cleanup;
    }
    
    /* Test 3: Single buffer operations */
    if (test_single_buffer_ops(backend, win) != 0) {
        printf("❌ Backend %s: Single buffer operations failed\n", backend->name);
        goto cleanup;
    }
    
    /* Test 4: Double buffer operations */
    test_double_buffer_ops(backend, ctx);
    
    /* Test 5: Hardware acceleration */
    test_hardware_acceleration(backend);
    
    printf("✅ Backend %s: Comprehensive testing completed\n", backend->name);
    
cleanup:
    if (win) screen_destroy_window(win);
    if (ctx) screen_destroy_context(ctx);
}

/* Print comprehensive test results */
void print_backend_analysis_summary(void) {
    printf("\n" "=" * 80 "\n");
    printf("🔬 COMPREHENSIVE SCREEN BACKEND ANALYSIS RESULTS\n");
    printf("=" * 80 "\n");
    
    for (int i = 0; i < g_backend_count; i++) {
        const screen_backend_config_t *backend = &g_backends[i];
        printf("\n📋 BACKEND: %s\n", backend->name);
        printf("   Description: %s\n", backend->description);
        
        /* Find all test results for this backend */
        int tests_run = 0, tests_passed = 0;
        for (int j = 0; j < g_result_count; j++) {
            if (strcmp(g_test_results[j].backend_name, backend->name) == 0) {
                tests_run++;
                if (g_test_results[j].result == 0) tests_passed++;
                
                printf("   %-25s: %s (%s)\n", 
                       g_test_results[j].test_name,
                       g_test_results[j].result == 0 ? "PASS" : "FAIL",
                       g_test_results[j].details);
            }
        }
        
        if (tests_run > 0) {
            printf("   Overall: %d/%d tests passed (%.1f%%)\n", 
                   tests_passed, tests_run, (float)tests_passed * 100.0f / tests_run);
        } else {
            printf("   Overall: No tests run\n");
        }
    }
    
    printf("\n🎯 RECOMMENDATIONS:\n");
    printf("   - Use backends with highest test pass rates\n");
    printf("   - Backends with buffer property access are suitable for double buffering\n");
    printf("   - Hardware acceleration backends preferred for performance\n");
    printf("=" * 80 "\n");
}

/* Main backend testing function */
int screen_backend_analysis_main(void) {
    printf("🧬 QNX Screen Backend Comprehensive Analysis\n");
    printf("==========================================\n");
    printf("Testing %d different screen backends systematically\n", g_backend_count);
    printf("Goal: Understand capabilities and limitations of each backend\n\n");
    
    /* Install signal handlers */
    signal(SIGBUS, signal_handler);
    signal(SIGSEGV, signal_handler);
    
    /* Test each backend comprehensively */
    for (int i = 0; i < g_backend_count; i++) {
        g_signal_caught = 0;
        test_backend_comprehensive(&g_backends[i]);
    }
    
    /* Print comprehensive analysis */
    print_backend_analysis_summary();
    
    return 0;
}

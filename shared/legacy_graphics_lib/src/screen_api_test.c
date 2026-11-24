/*
 * QNX Screen API Capability Testing - Methodical System Analysis
 * 
 * This file tests Screen API capabilities step-by-step to understand
 * what operations are supported by the current backend configuration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <screen/screen.h>

/* Test result tracking */
typedef struct {
    const char *test_name;
    int result;
    const char *error_msg;
} test_result_t;

static test_result_t g_test_results[20];
static int g_test_count = 0;

static void record_test(const char *name, int result, const char *error) {
    if (g_test_count < 20) {
        g_test_results[g_test_count].test_name = name;
        g_test_results[g_test_count].result = result;
        g_test_results[g_test_count].error_msg = error ? error : "OK";
        g_test_count++;
    }
}

static void print_test_summary(void) {
    printf("\n=== SCREEN API CAPABILITY TEST RESULTS ===\n");
    for (int i = 0; i < g_test_count; i++) {
        printf("%-40s: %s (%s)\n", 
               g_test_results[i].test_name,
               g_test_results[i].result == 0 ? "PASS" : "FAIL",
               g_test_results[i].error_msg);
    }
    printf("===========================================\n");
}

/* Test 1: Basic Screen Context Creation */
int test_context_creation(screen_context_t *ctx) {
    printf("🧪 Test 1: Screen Context Creation\n");
    
    int rc = screen_create_context(ctx, 0);
    if (rc != 0) {
        record_test("Context Creation", -1, "screen_create_context failed");
        return -1;
    }
    
    record_test("Context Creation", 0, NULL);
    printf("✅ Screen context created successfully\n");
    return 0;
}

/* Test 2: Window Creation with Different Configurations */
int test_window_creation(screen_context_t ctx, screen_window_t *win) {
    printf("🧪 Test 2: Window Creation\n");
    
    int rc = screen_create_window(win, ctx);
    if (rc != 0) {
        record_test("Window Creation", -1, "screen_create_window failed");
        return -1;
    }
    
    /* Test setting basic properties */
    int format = SCREEN_FORMAT_RGBA8888;
    rc = screen_set_window_property_iv(*win, SCREEN_PROPERTY_FORMAT, &format);
    if (rc != 0) {
        record_test("Set Format Property", -1, "SCREEN_PROPERTY_FORMAT failed");
    } else {
        record_test("Set Format Property", 0, NULL);
    }
    
    int size[2] = {640, 480};
    rc = screen_set_window_property_iv(*win, SCREEN_PROPERTY_SIZE, size);
    if (rc != 0) {
        record_test("Set Size Property", -1, "SCREEN_PROPERTY_SIZE failed");
    } else {
        record_test("Set Size Property", 0, NULL);
    }
    
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    rc = screen_set_window_property_iv(*win, SCREEN_PROPERTY_USAGE, &usage);
    if (rc != 0) {
        record_test("Set Usage Property", -1, "SCREEN_PROPERTY_USAGE failed");
    } else {
        record_test("Set Usage Property", 0, NULL);
    }
    
    record_test("Window Creation", 0, NULL);
    printf("✅ Window created successfully\n");
    return 0;
}

/* Test 3: Single Buffer Creation */
int test_single_buffer_creation(screen_window_t win) {
    printf("🧪 Test 3: Single Buffer Creation\n");
    
    int rc = screen_create_window_buffers(win, 1);
    if (rc != 0) {
        record_test("Single Buffer Creation", -1, "screen_create_window_buffers(1) failed");
        return -1;
    }
    
    record_test("Single Buffer Creation", 0, NULL);
    printf("✅ Single buffer created successfully\n");
    return 0;
}

/* Test 4: Buffer Handle Access */
int test_buffer_handle_access(screen_window_t win) {
    printf("🧪 Test 4: Buffer Handle Access\n");
    
    screen_buffer_t *buffers = NULL;
    int rc = screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc != 0) {
        record_test("Get Buffer Handles", -1, "SCREEN_PROPERTY_RENDER_BUFFERS failed");
        return -1;
    }
    
    if (!buffers || !buffers[0]) {
        record_test("Get Buffer Handles", -1, "Buffer handle is NULL");
        return -1;
    }
    
    record_test("Get Buffer Handles", 0, NULL);
    printf("✅ Buffer handle obtained: %p\n", buffers[0]);
    return 0;
}

/* Test 5: Buffer Property Access (Critical Test) */
int test_buffer_property_access(screen_window_t win) {
    printf("🧪 Test 5: Buffer Property Access (CRITICAL)\n");
    
    /* Get buffer handle first */
    screen_buffer_t *buffers = NULL;
    int rc = screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc != 0 || !buffers || !buffers[0]) {
        record_test("Buffer Property Access", -1, "Cannot get buffer handle");
        return -1;
    }
    
    /* Test pointer property access - this is where we get SIGBUS */
    void *buffer_ptr = NULL;
    printf("🔍 About to test screen_get_buffer_property_pv (SIGBUS location)...\n");
    rc = screen_get_buffer_property_pv(buffers[0], SCREEN_PROPERTY_POINTER, &buffer_ptr);
    if (rc != 0) {
        record_test("Buffer Property Access", -1, "SCREEN_PROPERTY_POINTER failed");
        printf("❌ Failed to get buffer pointer (rc=%d)\n", rc);
        return -1;
    }
    
    if (!buffer_ptr) {
        record_test("Buffer Property Access", -1, "Buffer pointer is NULL");
        return -1;
    }
    
    /* Test stride property access */
    int stride = 0;
    rc = screen_get_buffer_property_iv(buffers[0], SCREEN_PROPERTY_STRIDE, &stride);
    if (rc != 0) {
        record_test("Buffer Stride Access", -1, "SCREEN_PROPERTY_STRIDE failed");
    } else {
        record_test("Buffer Stride Access", 0, NULL);
        printf("✅ Buffer stride: %d\n", stride);
    }
    
    record_test("Buffer Property Access", 0, NULL);
    printf("✅ Buffer properties accessed successfully: ptr=%p\n", buffer_ptr);
    return 0;
}

/* Test 6: Double Buffer Creation */
int test_double_buffer_creation(screen_context_t ctx) {
    printf("🧪 Test 6: Double Buffer Creation\n");
    
    /* Create new window for double buffer test */
    screen_window_t test_win;
    int rc = screen_create_window(&test_win, ctx);
    if (rc != 0) {
        record_test("Double Buffer - Window", -1, "Window creation failed");
        return -1;
    }
    
    /* Set properties for double buffering */
    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(test_win, SCREEN_PROPERTY_FORMAT, &format);
    
    int size[2] = {640, 480};
    screen_set_window_property_iv(test_win, SCREEN_PROPERTY_SIZE, size);
    
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ;
    rc = screen_set_window_property_iv(test_win, SCREEN_PROPERTY_USAGE, &usage);
    if (rc != 0) {
        record_test("Double Buffer - Usage", -1, "Double buffer usage flags failed");
    } else {
        record_test("Double Buffer - Usage", 0, NULL);
    }
    
    /* Attempt double buffer creation */
    rc = screen_create_window_buffers(test_win, 2);
    if (rc != 0) {
        record_test("Double Buffer Creation", -1, "screen_create_window_buffers(2) failed");
        screen_destroy_window(test_win);
        return -1;
    }
    
    record_test("Double Buffer Creation", 0, NULL);
    printf("✅ Double buffers created successfully\n");
    
    /* Test accessing double buffer properties */
    screen_buffer_t *buffers = NULL;
    rc = screen_get_window_property_pv(test_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)&buffers);
    if (rc == 0 && buffers && buffers[0] && buffers[1]) {
        printf("✅ Both buffer handles valid: buf0=%p, buf1=%p\n", buffers[0], buffers[1]);
        
        /* Test accessing both buffer properties */
        void *ptr0 = NULL, *ptr1 = NULL;
        int rc0 = screen_get_buffer_property_pv(buffers[0], SCREEN_PROPERTY_POINTER, &ptr0);
        int rc1 = screen_get_buffer_property_pv(buffers[1], SCREEN_PROPERTY_POINTER, &ptr1);
        
        if (rc0 == 0 && rc1 == 0 && ptr0 && ptr1) {
            record_test("Double Buffer Properties", 0, NULL);
            printf("✅ Both buffer pointers valid: ptr0=%p, ptr1=%p\n", ptr0, ptr1);
        } else {
            record_test("Double Buffer Properties", -1, "Buffer pointer access failed");
        }
    } else {
        record_test("Double Buffer Handles", -1, "Buffer handles invalid");
    }
    
    screen_destroy_window(test_win);
    return 0;
}

/* Main test runner */
int screen_api_capability_test(void) {
    printf("🚀 QNX Screen API Capability Test - Methodical Analysis\n");
    printf("=======================================================\n");
    
    screen_context_t ctx = NULL;
    screen_window_t win = NULL;
    
    /* Run tests in sequence */
    if (test_context_creation(&ctx) != 0) {
        goto cleanup;
    }
    
    if (test_window_creation(ctx, &win) != 0) {
        goto cleanup;
    }
    
    if (test_single_buffer_creation(win) != 0) {
        goto cleanup;
    }
    
    if (test_buffer_handle_access(win) != 0) {
        goto cleanup;
    }
    
    /* This is the critical test - where we expect SIGBUS */
    printf("\n⚠️  CRITICAL TEST: Buffer Property Access (SIGBUS location)\n");
    printf("🔍 This test will show exactly where and why we get SIGBUS\n");
    test_buffer_property_access(win);
    
    /* Test double buffering capabilities */
    test_double_buffer_creation(ctx);
    
cleanup:
    if (win) screen_destroy_window(win);
    if (ctx) screen_destroy_context(ctx);
    
    print_test_summary();
    
    printf("\n🔬 Analysis Complete - System Capabilities Identified\n");
    return 0;
}

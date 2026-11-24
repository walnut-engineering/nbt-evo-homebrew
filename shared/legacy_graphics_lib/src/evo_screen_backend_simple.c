/*
 * EVO Screen Backend Management - Simplified Implementation
 * 
 * This simplified implementation provides just what we need to test 
 * different screen backends and learn their capabilities.
 */

#include "evo_screen_backend_simple.h"
#include <stdio.h>
#include <string.h>

/* Backend information database */
static const evo_backend_info_t backend_database[] = {
    {
        .type = EVO_BACKEND_BASIC,
        .name = "Basic",
        .description = "Basic libscreen.so.1 only",
        .library_name = "libscreen.so.1",
        .capabilities = EVO_CAP_DISPLAY,
        .priority = 10,
        .display_support = 1,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_PVR2D,
        .name = "PowerVR-2D",
        .description = "PowerVR SGX540 2D acceleration",
        .library_name = "screen-pvr2d.so",
        .capabilities = EVO_CAP_DISPLAY | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_HW_ACCEL | EVO_CAP_2D_ACCEL,
        .priority = 90,
        .display_support = 1,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_GLES2,
        .name = "OpenGL-ES-2.0",
        .description = "OpenGL ES 2.0 backend",
        .library_name = "screen-gles2.so",
        .capabilities = EVO_CAP_DISPLAY | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_HW_ACCEL | EVO_CAP_GLES2,
        .priority = 80,
        .display_support = 1,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_GLES1,
        .name = "OpenGL-ES-1.0",
        .description = "OpenGL ES 1.0 backend",
        .library_name = "screen-gles1.so",
        .capabilities = EVO_CAP_DISPLAY | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_HW_ACCEL | EVO_CAP_GLES1,
        .priority = 70,
        .display_support = 1,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_VIVANTE,
        .name = "Vivante",
        .description = "Vivante GC320 backend",
        .library_name = "screen-vivante-omap5430.so",
        .capabilities = EVO_CAP_DISPLAY | EVO_CAP_DOUBLE_BUFFER | EVO_CAP_HW_ACCEL | EVO_CAP_2D_ACCEL,
        .priority = 60,
        .display_support = 1,
        .last_error = NULL
    },
    {
        .type = EVO_BACKEND_SOFTWARE,
        .name = "Software",
        .description = "Software-only rendering",
        .library_name = "screen-sw.so",
        .capabilities = EVO_CAP_DISPLAY,
        .priority = 5,
        .display_support = 1,
        .last_error = NULL
    }
};

static const int backend_count = sizeof(backend_database) / sizeof(backend_database[0]);

evo_backend_info_t evo_backend_get_info(evo_backend_type_t type) {
    evo_backend_info_t empty = {0};
    
    for (int i = 0; i < backend_count; i++) {
        if (backend_database[i].type == type) {
            return backend_database[i];
        }
    }
    
    return empty;
}

const char* evo_backend_get_name(evo_backend_type_t type) {
    if (type == EVO_BACKEND_AUTO) {
        return "Auto-Select";
    }
    
    for (int i = 0; i < backend_count; i++) {
        if (backend_database[i].type == type) {
            return backend_database[i].name;
        }
    }
    
    return "Unknown";
}

int evo_backend_test_availability(evo_backend_type_t type) {
    /* For our testing purposes, assume all backends are potentially available */
    /* In a real implementation, this would test if the backend libraries exist */
    if (type >= EVO_BACKEND_BASIC && type < EVO_BACKEND_COUNT) {
        return 0;  /* Available */
    }
    return -1;  /* Not available */
}

evo_backend_type_t evo_backend_select_best(void) {
    /* Return the highest priority backend for testing */
    /* In a real implementation, this would test actual availability */
    return EVO_BACKEND_PVR2D;  /* Start with PowerVR for our testing */
}

void evo_backend_print_all_info(void) {
    printf("\n🔧 EVO Screen Backend Information:\n");
    printf("=====================================\n");
    
    for (int i = 0; i < backend_count; i++) {
        const evo_backend_info_t *info = &backend_database[i];
        printf("\n🔹 %s (%s)\n", info->name, info->description);
        printf("   Library: %s\n", info->library_name);
        printf("   Priority: %d/100\n", info->priority);
        printf("   Capabilities: 0x%02X\n", info->capabilities);
        printf("   - Display Support: %s\n", info->display_support ? "YES" : "NO");
        printf("   - Double Buffering: %s\n", (info->capabilities & EVO_CAP_DOUBLE_BUFFER) ? "YES" : "NO");
        printf("   - Hardware Accel: %s\n", (info->capabilities & EVO_CAP_HW_ACCEL) ? "YES" : "NO");
        printf("   - OpenGL ES 2.0: %s\n", (info->capabilities & EVO_CAP_GLES2) ? "YES" : "NO");
        printf("   - OpenGL ES 1.0: %s\n", (info->capabilities & EVO_CAP_GLES1) ? "YES" : "NO");
        printf("   - 2D Acceleration: %s\n", (info->capabilities & EVO_CAP_2D_ACCEL) ? "YES" : "NO");
    }
    
    printf("\n💡 Testing Strategy:\n");
    printf("   This system will test each backend systematically\n");
    printf("   to learn the exact capabilities and limitations.\n");
    printf("=====================================\n\n");
}

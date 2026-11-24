# EVO Graphics Library v2.0 - Complete API Reference

**Optimized Flat Structure | Unified Cairo+Text | Zero Redundancy**

## Overview

The EVO Graphics Library provides professional 2D graphics and text rendering for ARM QNX targets using Screen API and ILM compositor integration. This optimized version features a flat source structure with eliminated redundancy and unified Cairo+Text implementation.

## Library Structure (Optimized)

```
evo_graphics_lib/
├── include/           # Flat header structure
│   ├── evo_graphics_lib.h      # Master header (single include)
│   ├── evo_context.h           # Context management
│   ├── evo_display.h           # Display system
│   ├── evo_graphics.h          # Graphics primitives  
│   ├── evo_cairo.h             # Cairo integration
│   ├── evo_backend.h           # Backend management
│   └── evo_text.h              # Text rendering
├── src/               # Flat source structure
│   ├── evo_graphics_api.c      # Main API implementation
│   ├── evo_core_unified.c      # Core consolidation
│   ├── evo_display_unified.c   # Display management
│   ├── evo_backend_unified.c   # Backend abstraction
│   ├── evo_backend_display_unified.c  # Backend+Display (experimental)
│   ├── evo_cairo_text_unified.c # UNIFIED Cairo+Text (NEW)
│   └── fontconfig_embedded.h    # Embedded font config
└── examples/
    ├── unified_demo.c          # Complete usage example
    └── test_evo_performance_benchmark_suite.c
```

## Key Optimizations

- **Flat Structure**: All headers in `include/`, all sources in `src/`
- **Unified Cairo+Text**: Single file replaces `evo_cairo_unified.c` + `evo_text_unified.c`
- **Zero Redundancy**: Eliminated duplicate Cairo function pointers and surface management
- **Deprecated**: Removed `evo_graphics_lib_simple.h` and `evo_stubs.c` system
- **Single Include**: `#include "evo_graphics_lib.h"` is all you need

## Quick Start

```c
#include "evo_graphics_lib.h"

int main() {
    // Initialize library
    evo_graphics_init();
    
    // Create context with automatic backend selection
    evo_context_t* ctx = evo_context_create(800, 600, 0, 0);
    if (!ctx) return -1;
    
    // Clear screen and draw graphics
    evo_cairo_clear_screen(ctx, 0xFF000000);  // Black background
    evo_cairo_render_rect(ctx, 100, 100, 200, 150, 0xFF0000FF);  // Blue rectangle
    evo_cairo_render_circle(ctx, 400, 300, 75, 0xFF00FF00);      // Green circle
    
    // Render text (automatic Pango/Cairo fallback)
    evo_text_render(ctx, "EVO Graphics v2.0", 50, 50, 0xFFFFFFFF, "Arial", 24.0f);
    
    // Present to screen
    evo_context_present(ctx);
    
    // Cleanup
    evo_context_destroy(ctx);
    evo_graphics_shutdown();
    return 0;
}
```

## Core API Functions

### Library Management
```c
int evo_graphics_init(void);                    // Initialize library
void evo_graphics_shutdown(void);               // Cleanup library
const char* evo_graphics_get_version(void);     // Get version string
```

### Context Management
```c
evo_context_t* evo_context_create(int width, int height, int x, int y);
void evo_context_destroy(evo_context_t* ctx);
int evo_context_present(evo_context_t* ctx);    // Present framebuffer to screen
int evo_context_get_size(evo_context_t* ctx, int* width, int* height);
```

### Unified Graphics API (Cairo-Powered)
```c
// Screen management
int evo_cairo_clear_screen(evo_context_t* ctx, uint32_t color);

// Shape primitives  
int evo_cairo_render_rect(evo_context_t* ctx, int x, int y, int width, int height, uint32_t color);
int evo_cairo_render_circle(evo_context_t* ctx, int cx, int cy, int radius, uint32_t color);
int evo_cairo_render_line(evo_context_t* ctx, int x1, int y1, int x2, int y2, uint32_t color, float width);
```

### Unified Text API (Pango+Cairo)
```c
// Primary text function with automatic fallback (Pango → Cairo)
int evo_text_render(evo_context_t* ctx, const char* text, int x, int y, 
                   uint32_t color, const char* font_family, float font_size);

// Direct Cairo text rendering (simple)
int evo_cairo_draw_text_simple(const char* text, int x, int y, uint32_t color, float font_size);

// Direct Pango text rendering (advanced layout)
int evo_cairo_draw_text_pango(const char* text, int x, int y, uint32_t color, 
                             const char* font_family, float font_size);
```

## Color Format

All color values use 32-bit ARGB format:
```c
uint32_t color = 0xAARRGGBB;
// Examples:
#define RED    0xFFFF0000
#define GREEN  0xFF00FF00  
#define BLUE   0xFF0000FF
#define WHITE  0xFFFFFFFF
#define BLACK  0xFF000000
#define TRANSPARENT 0x00000000
```

## Backend Architecture

The library automatically selects the best available backend:

1. **Hardware Acceleration**: PowerVR GPU via `screen_pvr2d`
2. **Vivante GPU**: Vivante OMAP5430 hardware acceleration  
3. **Software Fallback**: CPU-based `screen_sw` renderer

Backend selection is transparent to applications - the same API works across all backends.

## Text Rendering Features

### Professional Typography Stack
- **Pango**: Advanced text layout, shaping, and international support
- **FontConfig**: Automatic font discovery and matching
- **FreeType2**: High-quality font rasterization
- **Cairo**: Vector-based text rendering with antialiasing

### Font Repository Access
- System fonts: `/fs/sda0/opt/conn/data/fonts/font_repository/` (25 fonts)
- Dynamic font loading and caching
- Automatic fallback font selection

### Text Features
- Unicode support with proper shaping
- Automatic font fallback (Pango → Cairo simple text)
- Antialiased text rendering
- Configurable font families and sizes
- Professional typographic quality

## Build System

### ARM Cross-Compilation
```bash
cd $HOME/nbt-evo-homebrew/shared/evo_graphics_lib

# Build optimized library
arm-unknown-nto-qnx7.1.0-gcc -shared -fPIC -O2 \
  src/evo_graphics_api.c \
  src/evo_core_unified.c \
  src/evo_display_unified.c \
  src/evo_backend_unified.c \
  src/evo_cairo_text_unified.c \
  -Iinclude \
  -lscreen -lm -ldl \
  -o libevo_graphics.so

# Build example  
arm-unknown-nto-qnx7.1.0-gcc -O2 \
  examples/unified_demo.c \
  -Iinclude -L. -levo_graphics \
  -o unified_demo
```

### Deployment
```bash
# Deploy to target
scp libevo_graphics.so unified_demo root@192.168.1.100:/tmp/

# Run on target
ssh root@192.168.1.100 'cd /tmp && LD_LIBRARY_PATH=/tmp ./unified_demo'
```

## Performance Characteristics

- **Hardware Acceleration**: Automatic GPU backend selection
- **Memory Efficiency**: Single Cairo surface shared between graphics and text
- **Zero Redundancy**: Unified function pointer management eliminates duplicates
- **Lazy Loading**: Libraries loaded only when needed
- **Optimal Batching**: Cairo operations batched for performance

## Architecture Benefits

### Unified Cairo+Text Implementation
- **Single Surface**: One Cairo surface for all rendering operations
- **Shared Context**: Graphics and text use same Cairo context
- **Unified Memory**: No duplicate framebuffer allocations
- **Consistent Quality**: Same antialiasing and color management

### Flat Structure Advantages  
- **Simplified Builds**: No nested include paths
- **Faster Compilation**: Reduced header scanning
- **Clear Dependencies**: Obvious file relationships
- **Maintainable Code**: Easy navigation and debugging

## Error Handling

All functions return standard error codes:
- `0`: Success
- `-1`: Invalid parameters
- `-2`: Initialization failure
- `-3`: Resource allocation failure
- `-4`: Backend unavailable

Use diagnostic output for debugging:
```c
// Enable detailed logging during development
printf("EVO Graphics Library v%s\n", evo_graphics_get_version());
```

## Migration from Previous Versions

### From v1.x to v2.0
1. Update includes: `#include "evo_graphics_lib.h"` (single include)
2. Replace `evo_text_*` calls with `evo_text_render()`
3. Replace separate Cairo calls with unified `evo_cairo_*` functions  
4. Remove stubs-related code (performance monitoring, etc.)
5. Update build system to use flat source structure

### Breaking Changes
- Removed `evo_graphics_lib_simple.h` 
- Removed `evo_stubs.c` performance monitoring functions
- Consolidated `evo_cairo_unified.c` + `evo_text_unified.c` → `evo_cairo_text_unified.c`
- Flattened include structure (no more `core/`, `backends/`, `text/` subdirs)

## Example Applications

See `examples/unified_demo.c` for complete usage including:
- Context creation and management
- Graphics primitives rendering  
- Professional text rendering
- Hardware acceleration testing
- Error handling patterns

## Library Dependencies

**Target System Requirements:**
- QNX 7.1.0 or later
- Screen API (`libscreen.so`)
- ILM compositor integration
- Optional: Cairo (`/fs/sda0/opt/conn/lib/browser/libcairo.so.2`)
- Optional: Pango, FontConfig, FreeType2 for advanced text

**Build Requirements:**
- ARM GCC cross-compiler toolchain
- QNX SDP 7.1.0 headers and libraries
- Standard C library with `dlopen()` support

## Support and Development

This optimized v2.0 implementation provides:
- ✅ Flat structure with zero redundancy
- ✅ Unified Cairo+Text rendering system  
- ✅ Single-include header design
- ✅ Professional typography support
- ✅ Hardware acceleration with automatic fallback
- ✅ Complete ARM QNX cross-compilation support

The library is designed for production deployment on ARM-based automotive and embedded systems running QNX with Screen API graphics acceleration.

# EVO Build System FAQ - Complete Reference Guide

## 📅 **LATEST UPDATES - July 22, 2025**

### ✅ **Phase 6.1: Screen_X Framework Integration Complete**
**Target**: `libscreen_X.cpp-ne.so` - Centralized Screen + ILM + EGL unified framework
- ✅ **Discovery**: All 5/5 Screen_X functions found and ready: `/opt/sys/lib/libscreen_X.cpp-ne.so`
- ✅ **Integration Test**: `test_screen_x_framework_integration` successfully added to backend tests
- ✅ **Framework Analysis**: Screen_X provides unified Screen+ILM coordination vs our current separate approach
- ✅ **Performance Baseline**: EVO current approach: 356ms for 100 operations (baseline established)

### ✅ **Phase 6.2: Communication Graphics Framework Integration Complete**
**Target**: `libcommunication_graphics.cpp-ne.so` - Graphics IPC and remote rendering framework
- ✅ **Discovery**: All 8/8 Communication Graphics functions found: `/opt/sys/lib/libcommunication_graphics.cpp-ne.so`
- ✅ **Integration Test**: `test_communication_graphics_framework` successfully added to backend tests
- ✅ **Framework Analysis**: Professional graphics IPC with RemoteEGLImage, shared memory, and signal coordination
- ✅ **Performance Baseline**: 2.00ms per operation, 100% success rate for all 8 functions
- 🎯 **Next Phase**: HMI Application Framework investigation for Phase 6.3

**Key Communication Graphics Functions Discovered:**
- `RemoteEGLImageSource` constructor - Remote EGL image source management
- `createImage` / `destroyImage` - Remote EGL image lifecycle management
- `shareMem` / `unshareMemEv` - Shared memory graphics buffer coordination
- `waitForUpdate` - Inter-process graphics synchronization
- `RemoteImageMetaData` constructor - Graphics metadata management
- `RemoteImageSignalCollector` constructor - Signal coordination framework

**Framework Capabilities:**
- 🚀 **Remote EGL Image Management**: Full EGL image sharing between processes
- 🎯 **Shared Memory Graphics IPC**: POSIX shared memory for graphics buffers
- 🔧 **Inter-Process Synchronization**: Signal-based coordination and updates
- 📈 **Graphics Metadata Management**: Comprehensive metadata for remote graphics

**Integration Benefits:**
- 🚀 **Multi-Process Graphics**: Enable graphics sharing between processes
- 🎯 **Hardware Acceleration**: Direct EGL image sharing maintains acceleration
- 🔧 **Professional IPC**: Enterprise-grade graphics communication framework
- 📈 **Scalability**: Support for distributed graphics architectures

**Key Screen_X Functions Discovered:**
- `screen_create_window` - Unified window creation with auto ILM integration
- `screen_set_window_property_iv` - Calls `ilm_surfaceSetVisibility`, `ilm_commitChanges` internally
- `screen_get_window_property_cv` - Property retrieval with ILM coordination
- Framework handles Screen + ILM + EGL coordination internally vs our external approach

**Screen_X Integration Benefits:**
- 🚀 **Simplified API**: Single library vs current Screen+ILM coordination
- 🎯 **Professional Integration**: Framework designed for unified operation  
- 🔧 **Reduced Complexity**: Eliminate custom Screen/ILM bridge code
- 📈 **Optimized Performance**: Internal coordination vs external coordination

## ⚠️ **CRITICAL SSH/TARGET STABILITY WARNING**

### ⚠️ NEVER RUN RAPID DEPLOYMENT TESTS - WILL CRASH SSH DAEMON ⚠️

**SSH Server Crash Prevention:**
- **Wait 30+ seconds between deployment tests**
- **Use single binary testing only**: `./scripts/evo_deploy_test.sh -t <ip> -b <binary>`
- **Monitor target responsiveness before additional tests**
- **Running multiple tests rapidly WILL crash the SSH daemon and require manual recovery**

### Q: How to run deployment tests safely?
**A: Safe Deployment Testing:**
```bash
# CORRECT: Single binary with proper delay
./scripts/evo_deploy_test.sh -t 169.254.199.119 -b unified_graphics_example
sleep 30  # MANDATORY WAIT
./scripts/evo_deploy_test.sh -t 169.254.199.119 -b simple_test

# WRONG: Multiple rapid tests (crashes SSH)
./scripts/evo_deploy_test.sh  # DON'T DO THIS
./scripts/evo_deploy_test.sh  # WILL CRASH TARGET
```

## �🔐 **TARGET SYSTEM CREDENTIALS & CONNECTION**

### Q: What are the EVO target system credentials?
**A: EVO Target System Details:**
- **Host/IP**: `169.254.199.119`
- **Username**: `root`
- **Password**: `CHANGEME`
- **System**: QNX 6.5.0 TI-OMAP5432-uEVM (armle)
- **Hardware**: PowerVR SGX540rev120 GPU (OMAP5430 chip)

### Q: How do I connect to the EVO target via SSH?
**A: SSH Connection Command:**
```bash
sshpass -p "CHANGEME" ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@169.254.199.119
```

**Why these SSH options?**
- EVO uses QNX 6.5.0 with legacy SSH implementation
- Requires explicit support for older key types: `ssh-rsa`, `ssh-dss`
- Modern SSH clients disable these by default for security

### Q: How do I transfer files to the EVO target?
**A: SCP File Transfer:**
```bash
# Upload file to target
sshpass -p "CHANGEME" scp -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10 <local_file> root@169.254.199.119:<remote_path>

# Download file from target
sshpass -p "CHANGEME" scp -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@169.254.199.119:<remote_file> <local_path>
```

### Q: How do I execute commands remotely on the EVO target?
**A: Remote Command Execution:**
```bash
sshpass -p "CHANGEME" ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@169.254.199.119 "<command>"

# Example: Check target system info
sshpass -p "CHANGEME" ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@169.254.199.119 "uname -a && cat /proc/version"
```

### Q: How do I set up environment variables for scripts?
**A: Environment Variables:**
```bash
# Add to your shell profile or script
export TARGET_IP="169.254.199.119"
export TARGET_USER="root"
export TARGET_PASS="CHANGEME"
export SSH_OPTS="-o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no -o ConnectTimeout=10"

# Usage in scripts
sshpass -p "$TARGET_PASS" ssh $SSH_OPTS "$TARGET_USER@$TARGET_IP" "command"
```

---

## 🔧 **BUILD SYSTEM & TOOLCHAIN**

### Q: What cross-compile toolchain should I use?
**A: ARM Linaro Toolchain:**
- **Location**: `$HOME/nbt-evo-homebrew/toolchain/arm-unknown-linux-gnueabi-linaro_4.7.3/`
- **Target**: `arm-unknown-linux-gnueabi`
- **Compiler**: `arm-unknown-linux-gnueabi-gcc`
- **Architecture**: ARM7TDMI-compatible for maximum compatibility
- **QNX Version**: 6.6.0 target system libraries

### Q: What are the proven build configuration patterns?
**A: Working Build Patterns (from successful projects):**

**EVO Library Design Pattern (Clean Architecture):**
```c
// ✅ GOOD: Implementation details in library
evo_context_t* ctx = evo_create_context(1440, 540, ARGB32);
evo_enable_text_rendering(ctx, "DejaVu Sans", 24);
evo_create_surface(ctx, surface_id, layer_id);
evo_render_text(ctx, "Test Message", x, y, color);
evo_screenshot(ctx, "test_proof.bmp");
evo_cleanup_context(ctx);

// ❌ AVOID: Low-level details in test code  
cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1440, 540);
cairo_t *cr = cairo_create(surface);
PangoLayout *layout = pango_cairo_create_layout(cr);
// ... 50+ lines of Cairo/Pango setup in every test
```
**Design Principle**: Test code should focus on A-B comparison logic, not implementation.
Move Cairo setup, ScreenX initialization, surface management into library functions.
Better for performance tuning and bug fixing as issues are discovered.

**DOOM EVO Pattern (Integer Math Only):**
```makefile
CFLAGS := -O0 -DDOOM_FIXED_POINT -march=armv7-a -mfpu=neon -mfloat-abi=hard
LDFLAGS := -lm -lc -lscreen
```
- Use `DOOM_FIXED_POINT` for integer-only math
- `-O0` optimization to avoid runtime conflicts
- No problematic builtin disabling

**Basic Window Pattern (Standard Optimization):**
```makefile
CFLAGS := -O1 -march=armv7-a -mfpu=neon -mfloat-abi=hard
LDFLAGS := -lm -lc -lscreen -lcpp
```
- Safe `-O1` optimization level
- QNX native library linking (`-lcpp` instead of `-lstdc++`)

**PROBLEMATIC Pattern (Causes SIGSEGV):**
```makefile
CFLAGS := -O1 -fno-builtin-sin -fno-builtin-cos -fno-builtin-sincos  # ❌ AVOID
```
- Disabling math builtins causes C++ runtime conflicts
- Results in crashes in `libstdc++.so.6`

### Q: What are the essential compiler and linker flags?
**A: Essential Build Flags:**

**Compiler Flags (CFLAGS):**
```makefile
CFLAGS := -O1 \
          -march=armv7-a \
          -mfpu=neon \
          -mfloat-abi=hard \
          -I$(SHARED_INCLUDE) \
          -I$(QNX_TARGET)/usr/include \
          -I$(QNX_TARGET)/usr/include/screen
```

**Linker Flags (LDFLAGS):**
```makefile
LDFLAGS := -L$(QNX_TARGET)/armle-v7/lib \
           -L$(QNX_TARGET)/armle-v7/usr/lib \
           -Wl,-rpath-link,$(QNX_TARGET)/armle-v7/lib \
           -Wl,-rpath-link,$(QNX_TARGET)/armle-v7/usr/lib \
           -lm -lc -lscreen -lcpp
```

**Required Libraries:**
- `-lscreen` - QNX Screen API (essential for display)
- `-lm` - Math library
- `-lc` - Standard C library  
- `-lcpp` - QNX C++ library (instead of `-lstdc++`)

### Q: Where are the QNX target libraries located?
**A: QNX Library Locations:**
```
# QNX 6.6.0 Target Libraries
$HOME/nbt-evo-homebrew/toolchain/qnx660/target/qnx6/armle-v7/lib/
$HOME/nbt-evo-homebrew/toolchain/qnx660/target/qnx6/armle-v7/usr/lib/

# EVO Target Runtime Libraries  
$HOME/nbt-evo-homebrew/toolchain/target/armle-v7/lib/
$HOME/nbt-evo-homebrew/toolchain/target/armle-v7/usr/lib/

# Headers
$HOME/nbt-evo-homebrew/toolchain/qnx660/target/qnx6/usr/include/
```

### Q: What build system organization should I follow?
**A: Project Structure:**
```
nbt-evo-homebrew/
├── projects/
│   ├── my_project/
│   │   ├── main.c              # Your main source
│   │   ├── Makefile            # Project-specific build
│   │   └── ...
├── shared/                     # Shared EVO libraries
│   ├── include/                # Shared headers
│   │   ├── evo_display_helper.h
│   │   ├── evo_graphics.h
│   │   ├── evo_text.h
│   │   └── evo_hw_graphics.h
│   └── src/                    # Shared implementations
├── bin/                        # All compiled binaries
├── scripts/                    # Deploy and test scripts
└── toolchain/                  # Cross-compile toolchain
```

---

## 📦 **DEPLOYMENT & TESTING**

### Q: How do I build and deploy my project?
**A: Build and Deploy Process:**

**1. Build Your Project:**
```bash
cd $HOME/nbt-evo-homebrew/projects/my_project
make clean && make all
```

**2. Deploy and Test:**
```bash
cd $HOME/nbt-evo-homebrew
./scripts/evo_deploy_test.sh test my_project
```

**3. Deploy All Projects:**
```bash
./scripts/evo_deploy_test.sh build     # Build all projects
./scripts/evo_deploy_test.sh test-all  # Test all binaries
```

### Q: What does the deploy script do automatically?
**A: Automated Deploy Process:**
1. **Discovers** all binaries in the `bin/` directory
2. **Transfers** binaries to EVO target (`/var/run/screen_tests/`)
3. **Sets** executable permissions on target
4. **Executes** binary with proper environment
5. **Captures** logs (stdout, stderr, execution)
6. **Validates** results based on configuration
7. **Generates** reports in `logs/deploy_test_TIMESTAMP/`

### Q: Where are deployment logs stored?
**A: Log Locations:**
```
$HOME/nbt-evo-homebrew/logs/deploy_test_YYYYMMDD_HHMMSS/
├── binary_name_execution.log    # Main execution log
├── binary_name_stdout.log       # Program stdout
├── binary_name_stderr.log       # Program stderr  
├── binary_name_slog.log         # QNX system log
└── binary_name_test.log         # Test framework output
```

### Q: How do I configure testing parameters?
**A: Test Configuration (scripts/test_config.json):**
```json
{
  "deployment": {
    "target": {
      "host": "169.254.199.119",
      "user": "root", 
      "password": "CHANGEME",
      "directory": "/var/run/screen_tests"
    }
  },
  "testing": {
    "default_timeout": 60
  },
  "binaries": {
    "custom_configs": {
      "my_binary": {
        "timeout": 120,
        "args": ["--param", "value"],
        "description": "My custom binary description"
      }
    }
  }
}
```

---

## 🎮 **HARDWARE GRAPHICS & POWERVRSGX540**

### Q: How do I enable hardware graphics acceleration?
**A: PowerVR SGX540 Integration:**

**1. Include Hardware Graphics Headers:**
```c
#include "evo_display_helper.h"  // Screen API
#include "evo_hw_graphics.h"     // Hardware acceleration
```

**2. Initialize Hardware Graphics:**
```c
evo_display_context_t *display = evo_display_create(1440, 540, 0, 0, 2000, 9999);
evo_hw_graphics_context_t *hw_graphics = evo_hw_graphics_init(display, NULL);

if (hw_graphics) {
    printf("Hardware graphics available!\n");
} else {
    printf("Using software fallback\n");
}
```

**3. Hardware Graphics Libraries (on target):**
```
/fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so      # PowerVR EGL
/fs/sda0/lib/graphics/omap5430_ES2_0/libImgGLESv2.so   # PowerVR OpenGL ES
```

### Q: Which Screen API backend should I use for hardware acceleration?
**A: QNX Screen Backend Selection (CRITICAL for proper hardware acceleration):**

**Available Screen Backends:**
```bash
# Primary Display (1440x540) - PowerVR SGX540
screen-pvr2d.so           # PowerVR 2D acceleration backend ⭐ RECOMMENDED
screen-gles2.so          # OpenGL ES 2.0 screen backend
libscreen.so.1           # Base screen library (basic functionality)

# Secondary Display (800x480) - Vivante GC320  
screen-vivante-omap5430.so  # Vivante backend (NOT for primary display)

# Other backends
screen-gles1.so          # OpenGL ES 1.x screen backend
screen-sw.so             # Software-only backend
wsegl-screen.so          # WSEGL (Window System EGL) integration
```

**Hardware Configuration (from graphics-1440_540.conf):**
```c
// Primary Display (1440x540) - PowerVR SGX540
begin egl-display 1
    egl-dlls = libIMGegl.so libImgGLESv2.so libglslcompiler.so libpvr2d.so
end egl-display

// Secondary Display (800x480) - Vivante GC320
begin egl-display 2  
    egl-dlls = libGalcore.so
end egl-display
```

**Critical Implementation:**
```makefile
# For PRIMARY display (1440x540) with PowerVR SGX540 - USE POWERVR BACKEND
LIBS := screen-pvr2d.so libscreen.so.1 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc

# For SECONDARY display (800x480) with Vivante GC320 - USE VIVANTE BACKEND  
LIBS := screen-vivante-omap5430.so libscreen.so.1 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

**Double Buffering Support:**
- **`screen-pvr2d.so`**: Native PowerVR double buffering with hardware acceleration
- **`screen-gles2.so`**: EGL-based buffer swapping via `eglSwapBuffers()`
- **`libscreen.so.1`**: Basic single buffering (causes flashing)

### Q: What PowerVR functions are available?
**A: IMG-Prefixed PowerVR Functions:**
- `IMGeglInitialize` - Initialize EGL display
- `IMGeglCreateContext` - Create OpenGL ES context  
- `IMGeglCreateWindowSurface` - Create rendering surface
- `IMGeglSwapBuffers` - Present rendered frame
- `IMGeglMakeCurrent` - Set active context

**Runtime Discovery Pattern:**
```c
void *powervr_lib = dlopen("/fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so", RTLD_LAZY);
if (powervr_lib) {
    PFNIMGEGLINITIALIZEPROC img_egl_init = dlsym(powervr_lib, "IMGeglInitialize");
    // Use img_egl_init instead of standard eglInitialize
}
```

### Q: What's the difference between software and hardware graphics?
**A: Graphics Rendering Comparison:**

**Software Graphics (evo_graphics.h):**
- ✅ Always available, no dependencies
- ✅ Consistent behavior across systems
- ❌ Limited performance for complex graphics
- ✅ Perfect for UI, text, simple shapes
- **Functions**: `evo_draw_line()`, `evo_draw_rect()`, `evo_draw_circle()`

**Hardware Graphics (evo_hw_graphics.h):**
- ⚡ GPU-accelerated performance
- ⚡ Full OpenGL ES 2.0 support
- ⚡ 3D graphics, shaders, textures
- ❓ Requires PowerVR SGX540 initialization
- **Fallback**: Automatic fallback to software when unavailable

---

## 🔤 **TEXT RENDERING SYSTEM**

### Q: What text rendering options are available?
**A: Text System Options:**

**Basic Text (evo_text.h) - DEPRECATED but maintained:**
```c
evo_draw_text(x, y, "Hello World", color, scale);  // 8x12 bitmap font
```

**Advanced Text (evo_text_advanced.h) - NEW:**
```c
evo_text_enhanced_draw_text(x, y, "Hello World", font_type, size, color);
```

**System Fonts Available:**
```
/usr/share/fonts/tt0003m_.ttf    # Monospace
/usr/share/fonts/tt0004m_.ttf    # Sans-serif regular  
/usr/share/fonts/tt0005m_.ttf    # Sans-serif bold
/usr/share/fonts/tt0006m_.ttf    # Serif regular
```

### Q: How do I use FreeType2 for advanced text rendering?
**A: FreeType2 Integration:**

**1. Link FreeType2 Library:**
```makefile
LDFLAGS += -lfreetype
```

**2. Advanced Text Features:**
- True Type Font (TTF) loading
- Font metrics and kerning
- Multi-line text layout
- Unicode character support
- Glyph caching for performance

**3. Text Layout Engine:**
```c
// Multi-font text with proper typography
evo_text_layout_t layout = evo_text_create_layout(width, height);
evo_text_add_font(layout, "/usr/share/fonts/tt0004m_.ttf", 16);
evo_text_render_paragraph(layout, text, alignment);
```

---

## 🛠 **TROUBLESHOOTING COMMON ISSUES**

### Q: My binary crashes with SIGSEGV in libstdc++.so.6, what's wrong?
**A: Math Library Conflicts:**
```
Process terminated SIGSEGV code=2 fltno=5 
ip=782ea318(/fs/sda0/lib/libstdc++.so.6@_ZN9__gnu_cxx18__exchange_and_addEPVii+0x40)
```

**Root Cause**: Problematic compiler flags disabling math builtins
**Solution**: Remove these flags from your Makefile:
```makefile
# REMOVE these problematic flags:
CFLAGS := -fno-builtin-sin -fno-builtin-cos -fno-builtin-sincos  # ❌ CAUSES CRASHES

# USE this instead:
CFLAGS := -O1  # or -O0 for DOOM pattern
```

### Q: PowerVR libraries load but function pointers are NULL, why?
**A: Symbol Resolution Issues:**
**Check**: Verify IMG-prefixed function names exist in library
```bash
# On target, inspect PowerVR library symbols
nm -D /fs/sda0/lib/graphics/omap5430_ES2_0/libIMGegl.so | grep egl
```

**Solution**: Use symbol inspector tool:
```bash
./scripts/evo_deploy_test.sh test symbol_inspector
```

### Q: Screen API initialization fails, what should I check?
**A: Screen Service Prerequisites:**
1. **Check ILM Service**: `pidin | grep ilm`
2. **Verify Screen Service**: `pidin | grep screen`
3. **Test Basic Window**: `./scripts/evo_deploy_test.sh test basic_window`

**Working Screen Initialization Pattern:**
```c
screen_create_context(&screen_ctx, 0);
screen_create_window(&screen_win, screen_ctx);
screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_FORMAT, &format);
screen_create_window_buffers(screen_win, 1);
```

### Q: How do I debug deployment and connection issues?
**A: Debug Steps:**
1. **Test Network**: `ping 169.254.199.119`
2. **Test SSH**: `ssh root@169.254.199.119` (manual login)
3. **Check sshpass**: `which sshpass` (must be installed)
4. **Test Legacy SSH**: Use full SSH options from credentials reference
5. **Check Target Space**: `df -h` on target (ensure sufficient space)

---

## 📋 **QUICK REFERENCE COMMANDS**

### Essential Commands:
```bash
# Build and test specific project
cd projects/my_project && make clean && make all
cd ../.. && ./scripts/evo_deploy_test.sh test my_project

# Test all projects
./scripts/evo_deploy_test.sh test-all

# Manual SSH connection
sshpass -p "CHANGEME" ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-dss -o StrictHostKeyChecking=no root@169.254.199.119

# Check deploy logs
ls -la logs/deploy_test_*/
cat logs/deploy_test_*/my_binary_execution.log

# Test hardware graphics
./scripts/evo_deploy_test.sh test hw_graphics_demo
```

### Project Templates:
- **Basic Application**: Copy `projects/basic_window/` structure
- **Graphics Demo**: Copy `projects/graphics_demo/` structure  
- **Hardware Graphics**: Copy `projects/hw_graphics_demo/` structure
- **Text System**: Copy `projects/text_demo/` structure

## 🏗️ **EVO Library Architecture Guidelines**

### Q: How should implementation details be organized in EVO library vs test code?
**A: Clean Architecture Separation Principles:**

**Library Layer (EVO Internal Implementation):**
```c
// File: src/evo_internal.c
// ✅ All Cairo, ScreenX, ILM setup belongs in library
evo_context_t* evo_create_context(int width, int height, format_t format) {
    evo_context_t *ctx = malloc(sizeof(evo_context_t));
    
    // Cairo initialization - complex but hidden from users
    ctx->cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    ctx->cairo_context = cairo_create(ctx->cairo_surface);
    
    // ScreenX vs Screen+ILM detection and setup
    if (screenx_available()) {
        ctx->use_screenx = true;
        ctx->screenx_ctx = screen_create_context(SCREEN_APPLICATION_CONTEXT);
    } else {
        ctx->use_screenx = false;
        ctx->screen_ctx = screen_create_context(SCREEN_APPLICATION_CONTEXT);
        ilmErrorTypes ilm_result = ilm_init();
    }
    
    return ctx;
}

void evo_enable_text_rendering(evo_context_t *ctx, const char *font_family, int size) {
    // All Pango/FreeType2 complexity hidden in library
    ctx->pango_layout = pango_cairo_create_layout(ctx->cairo_context);
    PangoFontDescription *font_desc = pango_font_description_from_string(font_family);
    pango_font_description_set_size(font_desc, size * PANGO_SCALE);
    pango_layout_set_font_description(ctx->pango_layout, font_desc);
}
```

**Test Layer (Clean A-B Logic):**
```c
// File: test_evo_v3_ab_screen_vs_screenx.c
// ✅ Tests focus on comparison logic, not implementation details
void test_a_evo_unified_baseline() {
    evo_context_t* ctx = evo_create_context(1440, 540, ARGB32);
    evo_enable_text_rendering(ctx, "DejaVu Sans", 24);
    evo_create_surface(ctx, 2000, 9000);
    
    for (int frame = 1; frame <= 15; frame++) {
        evo_render_text(ctx, "TEST A: EVO v2/v3 UNIFIED", 100, 100, WHITE);
        if (frame == 15) {
            evo_screenshot(ctx, "test_a_evo_v3_green_proof.bmp");
        }
        usleep(100000);
    }
    
    evo_cleanup_context(ctx);  // All cleanup handled internally
}

void test_b_screenx_implementation() {
    evo_context_t* ctx = evo_create_context_screenx(1440, 540, ARGB32);  // Force ScreenX
    evo_enable_text_rendering(ctx, "DejaVu Sans", 24);
    evo_create_surface(ctx, 2001, 9000);
    
    // Same test logic - library handles ScreenX vs Screen+ILM differences
    for (int frame = 1; frame <= 15; frame++) {
        evo_render_text(ctx, "TEST B: SCREENX UNIFIED", 100, 100, WHITE);
        if (frame == 15) {
            evo_screenshot(ctx, "test_b_screenx_green_proof.bmp");
        }
        usleep(100000);
    }
    
    evo_cleanup_context(ctx);
}
```

**Benefits of Library Abstraction:**
- 🚀 **Performance Tuning**: Optimize Cairo/ScreenX code once in library, all tests benefit
- 🎯 **Bug Fixing**: Fix cleanup crashes in library layer, not scattered across test files  
- 🔧 **Maintainability**: Test code focuses on A-B logic, not 100+ lines of setup/cleanup
- 📈 **Consistency**: Same initialization patterns across all EVO applications

**Migration Strategy:**
1. Move Cairo + Pango setup from test files → `evo_text_rendering.c`
2. Move ScreenX detection/init from test files → `evo_context.c`  
3. Move cleanup logic from test files → `evo_cleanup.c`
4. Test files become clean A-B comparison logic only

---

## 📚 **DOCUMENTATION REFERENCES**

### Key Files:
- **Credentials**: `/projects/EVO_CREDENTIALS_REFERENCE.md`
- **Toolchain Guide**: `/toolchain/README.md`
- **Hardware Graphics**: `/shared/HARDWARE_GRAPHICS_EXAMPLE.md`
- **Build Patterns**: Working Makefiles in `doom_evo/`, `basic_window/`
- **Deploy Scripts**: `/scripts/evo_deploy_test.sh`, `/scripts/test_config.json`

### System Information:
- **Target System**: QNX 6.5.0 TI-OMAP5432-uEVM (armle)
- **Hardware GPU**: PowerVR SGX540rev120 
- **Screen Resolution**: 1440x540 (automotive display)
- **Color Format**: RGBA8888 (IMG_FMT_PKLE_ABGR8888)
- **Available Memory**: Check with `cat /proc/meminfo` on target

---

**REMEMBER**: Always use `sshpass` with legacy SSH options, follow proven build patterns, and test with the deploy script for consistent results!

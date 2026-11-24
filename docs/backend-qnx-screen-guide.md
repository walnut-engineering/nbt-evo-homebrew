# QNX Screen API Backend Selection Guide

## 🎯 **CRITICAL DISCOVERY**: Hardware-Specific Screen Backends

The QNX Screen API uses specialized backend libraries for different hardware configurations. **Using the wrong backend causes flashing, poor performance, and missing hardware acceleration.**

---

## 📺 **EVO TARGET DISPLAY CONFIGURATION**

### **Primary Display (1440x540)** - PowerVR SGX540
- **Resolution**: 1440 x 540 @ 60Hz
- **GPU**: PowerVR SGX540rev120 
- **Libraries**: `libIMGegl.so`, `libImgGLESv2.so`, `libpvr2d.so`
- **Format**: RGBA8888
- **Screen Backend**: **`screen-pvr2d.so`** ⭐ **REQUIRED FOR HARDWARE ACCELERATION**

### **Secondary Display (800x480)** - Vivante GC320  
- **Resolution**: 800 x 480 @ 60Hz
- **GPU**: Vivante GC320 (2D acceleration)
- **Libraries**: `libGalcore.so`
- **Format**: RGBA8888
- **Screen Backend**: **`screen-vivante-omap5430.so`**

---

## 🔧 **SCREEN BACKEND SELECTION**

### **For Primary Display Applications (MOST COMMON)**
```makefile
# Use PowerVR SGX540 backend for 1440x540 primary display
LIBS := ../../toolchain/target/lib/screen-pvr2d.so \
        ../../toolchain/target/lib/libscreen.so.1 \
        -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

**Why screen-pvr2d.so?**
- ✅ Native PowerVR hardware acceleration
- ✅ Hardware double buffering (eliminates flashing)
- ✅ Optimal performance for 1440x540 display
- ✅ Direct integration with PowerVR SGX540 GPU

### **For Secondary Display Applications (RARE)**
```makefile
# Use Vivante backend for 800x480 secondary display
LIBS := ../../toolchain/target/lib/screen-vivante-omap5430.so \
        ../../toolchain/target/lib/libscreen.so.1 \
        -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

### **For OpenGL ES Applications**
```makefile
# Use OpenGL ES 2.0 screen backend
LIBS := ../../toolchain/target/lib/screen-gles2.so \
        ../../toolchain/target/lib/libscreen.so.1 \
        -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

---

## 📋 **AVAILABLE SCREEN BACKENDS**

| Backend | Purpose | Hardware | Display | Performance |
|---------|---------|----------|---------|-------------|
| **`screen-pvr2d.so`** | PowerVR 2D acceleration | SGX540 | 1440x540 | ⭐⭐⭐ |
| **`screen-gles2.so`** | OpenGL ES 2.0 | SGX540 | 1440x540 | ⭐⭐⭐ |
| **`screen-vivante-omap5430.so`** | Vivante 2D | GC320 | 800x480 | ⭐⭐ |
| **`screen-gles1.so`** | OpenGL ES 1.x | SGX540 | 1440x540 | ⭐⭐ |
| **`wsegl-screen.so`** | EGL Window System | SGX540 | 1440x540 | ⭐⭐⭐ |
| **`libscreen.so.1`** | Base library | Any | Any | ⭐ |

---

## 🎮 **HARDWARE GRAPHICS INTEGRATION**

### **Graphics Configuration Discovery**
From `/toolchain/target/lib/graphics/omap5430_ES2_0/graphics-1440_540.conf`:

```c
begin winmgr
   begin display 1
      formats = rgba8888 rgbx8888 nv12  
      video-mode = 1440 x 540 @ 60
   end display
   
   begin display 2
      formats = rgba8888 rgbx8888 nv12
      video-mode = 800 x 480 @ 60  
   end display
end winmgr

begin egl-display 1
    egl-dlls = libIMGegl.so libImgGLESv2.so libglslcompiler.so libpvr2d.so
end egl-display

begin egl-display 2
    egl-dlls = libGalcore.so  
end egl-display
```

### **Double Buffering Implementation**
```c
// Enable PowerVR hardware double buffering
evo_display_enable_double_buffering(display_context, 1);

// Render to back buffer
uint32_t *back_buffer = evo_display_get_next_framebuffer(display_context);
// ... render graphics to back_buffer ...

// Swap buffers (hardware accelerated)
evo_display_swap_buffers(display_context);
```

---

## ⚠️ **COMMON MISTAKES**

### **❌ Wrong Backend for Primary Display**
```makefile
# WRONG - Using Vivante for primary display causes poor performance
LIBS := screen-vivante-omap5430.so libscreen.so.1 -lm -lc
```

### **❌ Missing Backend Library**
```makefile  
# WRONG - Only base library causes flashing and no hardware acceleration
LIBS := libscreen.so.1 -lm -lc
```

### **❌ Incorrect Display Configuration**
```c
// WRONG - Using 800x480 parameters on primary display
display = evo_display_create(800, 480, 0, 0, surface_id, layer_id);
```

### **✅ Correct Configuration**
```makefile
# CORRECT - PowerVR backend for primary display with double buffering
LIBS := screen-pvr2d.so libscreen.so.1 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

```c
// CORRECT - Primary display configuration
display = evo_display_create(1440, 540, 0, 0, 2100, 10000);
evo_display_enable_double_buffering(display, 1);
```

---

## 🔍 **VERIFICATION COMMANDS**

### **Check Available Backends (on target)**
```bash
# List all available screen backends
ls -la /fs/sda0/opt/sys/lib/screen*.so*
ls -la /opt/sys/lib/screen*.so*
```

### **Check Graphics Configuration**
```bash
# Display current graphics configuration
cat /lib/graphics/omap5430_ES2_0/graphics-1440_540.conf
```

### **Verify Hardware Detection**
```bash
# Check if PowerVR is detected
dmesg | grep -i pvr
dmesg | grep -i sgx
```

### **Runtime Library Loading**
```bash
# Check which screen library is being loaded
ldd /var/run/screen_tests/your_binary | grep screen
```

---

## 📚 **IMPLEMENTATION EXAMPLES**

### **Proven Working Pattern (hw_graphics_demo)**
```makefile
# Primary display with PowerVR acceleration
LIBS := ../../toolchain/target/lib/libscreen.so.1 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

### **Enhanced Pattern with Hardware Double Buffering**
```makefile
# Primary display with PowerVR 2D acceleration backend
LIBS := ../../toolchain/target/lib/screen-pvr2d.so \
        ../../toolchain/target/lib/libscreen.so.1 \
        -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

### **Full OpenGL ES Pattern**
```makefile
# Primary display with OpenGL ES 2.0 backend
LIBS := ../../toolchain/target/lib/screen-gles2.so \
        ../../toolchain/target/lib/libscreen.so.1 \
        -lEGL -lGLESv2 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

---

## 🎯 **RECOMMENDATION**

**For most EVO applications targeting the primary display (1440x540):**

1. **Use `screen-pvr2d.so`** for PowerVR SGX540 hardware acceleration
2. **Enable double buffering** to eliminate flashing
3. **Use proven display parameters**: 1440x540, surface_id=2100, layer_id=10000
4. **Include proper runtime library paths** in the test runner

This configuration provides optimal performance with hardware acceleration and eliminates the flashing issues caused by single buffering.

---

**Last Updated**: July 19, 2025  
**Verified Configuration**: PowerVR SGX540rev120 on QNX 6.5.0 TI-OMAP5432-uEVM

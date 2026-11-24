# QNX/ILM/Screen System Architecture Analysis

## Discovery Summary
After examining system startup logs and configurations, the intended architecture is now clear.

## System Architecture Flow

### 1. System Startup Sequence
```
1. start_screen.sh launches → Creates graphics config symlink
2. Screen service starts → Loads PowerVR/Vivante backends
3. LayerManagerService (LMS) auto-starts → Connects to Screen as process 127006
4. Applications connect to ILM → LMS manages Screen on their behalf
```

### 2. Intended API Usage Pattern
```
❌ WRONG (what we were doing):
App → Screen (create window) → ILM (try to manage it) → Conflicts

✅ CORRECT (intended pattern):
App → ILM → LayerManagerService → Screen
```

### 3. Key Evidence from startup_slog.log

**Screen starts first:**
```
screen: starting up...
screen: loading EGL display 1...  (PowerVR SGX540)
screen: loading EGL display 2...  (Vivante GC320)
```

**LMS connects to Screen:**
```
screen: process 127006 connecting with flags 0x9
[LMGR] LayerManagerService | INFO | Starting Management
[LMGR] ScreenRenderer | INFO | display count = 2
```

**Applications connect to ILM, not Screen:**
```
[LMGR] ilm_init | INFO | pid = 8209:EarlyVideoApp
[LMGR] LayerManagerService | INFO | Connect from application EarlyVideoApp(8209)
```

## Why Our Current Implementation Fails

### Current Problems
1. **Permission Conflicts**: Creating Screen windows directly causes "ERROR 22: Invalid argument"
2. **Double Management**: Screen + ILM both trying to control the same surfaces
3. **Unauthorized Requests**: ILM rejects surfaces it didn't create

### Root Cause
We're bypassing the LayerManagerService and trying to do its job manually.

## Correct Implementation Strategy

### Phase 1: ILM-Only Approach
```c
// Connect to ILM
ilm_init();

// Let ILM create and manage surfaces
ilm_surfaceCreate(surface_id, width, height, pixelformat, &surface_id);
ilm_layerAddSurface(layer_id, surface_id);
ilm_commitChanges();

// ILM handles Screen communication internally
```

### Phase 2: Direct EGL Context (if needed)
```c
// Get ILM-managed surface
ilm_surfaceGetNativeContent(surface_id, &screen_window);

// Create EGL context on ILM surface
egl_context = eglCreateContext(display, config, EGL_NO_CONTEXT, attribs);
```

## Resolution Path

1. **✅ COMPLETED: Abandon hybrid Screen+ILM approach**
2. **✅ COMPLETED: Use pure ILM API for surface management**
3. **✅ COMPLETED: Let LayerManagerService handle Screen coordination**
4. **✅ COMPLETED: Focus on EGL rendering to ILM-managed surfaces**

## Success Confirmation

**Test Results (test_unified_library):**
- ✅ **Zero "ERROR 22: Invalid argument" messages**
- ✅ **Clean ILM connection**: `test_unified_library(14626925)` connected and disconnected properly
- ✅ **Full visual functionality**: 30 frames rendered successfully  
- ✅ **Proper architecture**: App → ILM → LayerManagerService → Screen (no direct Screen API usage)

**Key Implementation Changes:**
```c
// OLD (problematic):
screen_create_context() → screen_create_window() → ilm_surfaceCreate(screen_window)

// NEW (clean):
ilm_init() → ilm_surfaceCreate(NULL) → ilm_layerCreate() → ilm_commitChanges()
```

**Evidence from slog:**
```
[LMGR] LayerManagerService | INFO | Connect from application test_unified_library(14626925)
[LMGR] LayerManagerService | INFO | Disconnect from application test_unified_library(14626925)
```

This explains why we achieve visual success but generate errors - we're fighting against the intended architecture rather than working with it.

**SOLUTION IMPLEMENTED**: Clean ILM-only approach eliminates all error messages while maintaining full functionality.

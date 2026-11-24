# EVO Build System - CRITICAL KNOWLEDGE FOR AGENTS

## 🚨 **CRITICAL WARNING FOR AI AGENTS**

**DO NOT MODIFY THE PROVEN BUILD PATTERNS WITHOUT UNDERSTANDING THE COMPLEXITY!**

This document contains hard-learned lessons about the EVO cross-compilation environment. Follow these patterns EXACTLY or you will waste hours debugging toolchain conflicts.

---

## 🏗️ **PROVEN WORKING BUILD PATTERN**

### ✅ **EXACT Working Configuration (DO NOT CHANGE):**

From `projects/basic_window/Makefile` - this pattern WORKS:

```makefile
# EVO toolchain paths (EXACT - DO NOT CHANGE)
QNX_BASE := ../../toolchain/qnx660
QNX_LIB := $(QNX_BASE)/armle-v7/lib
QNX_LIB_USR := $(QNX_BASE)/armle-v7/usr/lib
QNX_INCLUDE := $(QNX_BASE)/usr/include
QNX_INCLUDE_USR := $(QNX_BASE)/armle-v7/usr/include

# EVO-specific toolchain (EXACT - DO NOT CHANGE)
CC := ../../toolchain/arm-unknown-linux-gnueabi-linaro_4.7.3/bin/arm-unknown-linux-gnueabi-gcc
STRIP := ../../toolchain/arm-unknown-linux-gnueabi-linaro_4.7.3/bin/arm-unknown-linux-gnueabi-strip

# EVO-specific CFLAGS (EXACT - DO NOT CHANGE)
CFLAGS := -mcpu=arm7tdmi -marm -msoft-float -mno-unaligned-access \
          -mstructure-size-boundary=8 -mno-thumb-interwork -mno-sched-prolog \
          -fno-schedule-insns -fno-schedule-insns2 -O1 \
          -mapcs-frame -mno-apcs-stack-check \
          -fno-strict-aliasing -fno-builtin -fno-inline -fno-common \
          -I$(SHARED_INCLUDE) -I$(QNX_INCLUDE) -I$(QNX_INCLUDE_USR) \
          -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ \
          -D_QNX_SOURCE -DEVO_SCREEN_EXAMPLES \
          -Wno-implicit-int -Wno-implicit-function-declaration -Wno-unused-variable \
          -Wall -std=c99

# EVO CRT and linking setup (EXACT - DO NOT CHANGE)
CRT_START := $(QNX_LIB)/crt1.o $(QNX_LIB)/crti.o
CRT_END := $(QNX_LIB)/crtn.o

# EVO-specific LDFLAGS (EXACT - DO NOT CHANGE)
LDFLAGS := -L$(QNX_LIB) -L$(QNX_LIB_USR) \
           -L../../toolchain/target/lib \
           -L../../toolchain/target/opt/sys/lib \
           -nostdlib \
           -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2 \
           -Wl,-rpath-link,$(QNX_LIB) \
           -Wl,-rpath-link,$(QNX_LIB_USR) \
           -Wl,-rpath-link,../../toolchain/target/lib \
           -Wl,-rpath-link,../../toolchain/target/opt/sys/lib \
           -Wl,-rpath,/fs/sda0/opt/sys/lib \
           -Wl,-rpath,/opt/sys/lib

# Libraries (EXACT - DO NOT CHANGE BASE SET)
LIBS := ../../toolchain/target/lib/libscreen.so.1 -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

---

## ❌ **CRITICAL MISTAKES TO AVOID**

### **1. DO NOT Mix Toolchains**
```makefile
# ❌ NEVER DO THIS - Causes "multiple definition of _start" errors
LIBS := -lc -ldl -lm -lgcc  # This uses ARM Linaro libs
```

### **2. DO NOT Use Simple GCC Flags**
```makefile
# ❌ NEVER DO THIS - Creates wrong binary type  
CFLAGS := -O1 -march=armv7-a -mfpu=neon -mfloat-abi=hard  # GNU/Linux binary!
```

### **3. DO NOT Skip -nostdlib**
```makefile
# ❌ NEVER DO THIS - Causes library conflicts
LDFLAGS := -L$(QNX_LIB)  # Missing -nostdlib!
```

### **4. DO NOT Use Wrong Dynamic Linker**
```makefile
# ❌ WRONG - Creates GNU/Linux binary that won't run on QNX
# Missing: -Wl,--dynamic-linker=/usr/lib/ldqnx.so.2
```

---

## 🎯 **PROPER BINARY IDENTIFICATION**

### ✅ **Correct QNX Binary:**
```
file ../../bin/backend_analyzer
../../bin/backend_analyzer: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), 
dynamically linked, interpreter /usr/lib/ldqnx.so.2, for GNU/Linux 2.6.16, stripped
```

### ❌ **Wrong GNU/Linux Binary:**
```
file ../../bin/simple_backend_analyzer  
../../bin/simple_backend_analyzer: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), 
dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 2.6.32, not stripped
```

**Key Difference**: `interpreter /usr/lib/ldqnx.so.2` vs `/lib/ld-linux.so.3`

---

## 🔧 **ADDING NEW LIBRARIES SAFELY**

### ✅ **Safe Way to Add dlopen Support:**
```makefile
# Check if library exists in target first
ls ../../toolchain/target/lib/libdl.so.2
ls ../../toolchain/target/opt/sys/lib/libdl.so.2

# If found, add to LIBS with exact path:
LIBS := ../../toolchain/target/lib/libscreen.so.1 \
        ../../toolchain/target/opt/sys/lib/libdl.so.2 \
        -lsocket -l:libm.so.2 -l:libc.so.3 -lgcc
```

### ❌ **Wrong Way:**
```makefile
# ❌ NEVER - Uses ARM Linaro libdl which depends on libc.so.6
LIBS := $(BASE_LIBS) -ldl
```

---

## 🚨 **COMMON ERROR PATTERNS**

### **Error 1: Multiple Definition of _start**
```
multiple definition of `_start'
/toolchain/arm-unknown-linux-gnueabi-linaro_4.7.3/.../crt1.o::(.text+0x0): first defined here
```
**Cause**: Mixing ARM Linaro CRT with QNX CRT
**Fix**: Use EXACT proven pattern, don't modify CRT setup

### **Error 2: TLS Definition Mismatches**
```
errno: TLS definition in .../libc.so.6 section .tbss mismatches non-TLS definition in .../libc.so section .bss
```
**Cause**: Mixing GNU libc.so.6 with QNX libc.so.3
**Fix**: Use `-l:libc.so.3` instead of `-lc`

### **Error 3: Can't Access Shared Library (Target)**
```
/var/run/screen_tests/binary: Can't access shared library
```
**Cause**: Built GNU/Linux binary instead of QNX binary
**Fix**: Check `file` output - must have `interpreter /usr/lib/ldqnx.so.2`

---

## 📋 **SAFE MODIFICATION PROCEDURE**

### **Step 1: Copy Proven Working Makefile**
```bash
cp projects/basic_window/Makefile projects/my_project/Makefile
```

### **Step 2: Change ONLY These Safe Fields**
```makefile
PROJECT_NAME := my_new_project    # ✅ Safe to change
MAIN_SOURCE := my_source.c        # ✅ Safe to change
```

### **Step 3: Test Build**
```bash
make clean && make all
file ../../bin/my_new_project     # Must show /usr/lib/ldqnx.so.2
```

### **Step 4: If Adding Libraries**
- First check: `find ../../toolchain/target -name "libnewlib*"`
- Use exact path: `../../toolchain/target/path/to/libnewlib.so.X`
- Test on target: Deploy and verify it runs

---

## 🎯 **DEPLOYMENT VERIFICATION**

### **Working Deployment Process:**
```bash
# 1. Build with proven pattern
make -f Makefile.working clean && make -f Makefile.working all

# 2. Verify QNX binary  
file ../../bin/backend_analyzer | grep "/usr/lib/ldqnx.so.2"

# 3. Deploy to target
cd ../.. && $HOME/nbt-evo-homebrew/scripts/evo_deploy_test.sh test backend_analyzer

# 4. Check execution (should NOT show "Can't access shared library")
```

---

## 💡 **WHY THIS COMPLEXITY EXISTS**

### **The EVO Cross-Compilation Challenge:**
1. **ARM Linaro Toolchain**: Designed for GNU/Linux ARM targets
2. **QNX Target System**: Uses different ABI, different libc, different dynamic linker
3. **Mixed Environment**: We use ARM Linaro compiler with QNX libraries
4. **Critical Balance**: Must use QNX libraries/CRT but ARM Linaro compiler

### **Why You Can't Just Change Flags:**
- ARM7TDMI flags ensure compatibility with OMAP5432
- `-nostdlib` prevents ARM Linaro stdlib from conflicting with QNX
- QNX CRT files provide proper QNX startup sequence
- QNX dynamic linker path ensures proper runtime loading

---

## 🚀 **AGENT SUCCESS PATTERN**

### **DO THIS:**
1. Copy proven working Makefile exactly
2. Change only PROJECT_NAME and MAIN_SOURCE
3. Test binary type with `file` command
4. Deploy and verify execution

### **DON'T DO THIS:**
1. Try to "improve" the CFLAGS or LDFLAGS
2. Use simple GCC patterns from tutorials
3. Mix different toolchain components
4. Assume standard Linux build patterns work

---

## 📚 **REFERENCES TO PROVEN WORKING PROJECTS**

### **Base Patterns (COPY THESE):**
- `projects/basic_window/Makefile` - Simple application pattern
- `projects/simple_test/Makefile` - Minimal test pattern  
- `projects/doom_evo/Makefile` - Complex application pattern

### **Working Binaries to Examine:**
```bash
file ../../bin/simple_test        # ✅ Proper QNX binary
file ../../bin/basic_window       # ✅ Proper QNX binary  
file ../../bin/gears_demo         # ✅ Proper QNX binary
```

---

## 🏆 **FINAL AGENT GUIDANCE**

**NEVER start from scratch with build configuration!**

**ALWAYS start by copying a proven working Makefile!**

**ALWAYS verify binary type before deployment!**

**The EVO build system works perfectly when you follow the proven patterns exactly.**

---

*This knowledge was learned through extensive trial and error. Respect the complexity and follow the proven patterns to save hours of debugging time.*

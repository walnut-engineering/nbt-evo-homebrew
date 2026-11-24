/* evo_glfwd - probe the GENIVI FORWARDING EGL (libEGL_X) that the HMI apps use.
 *
 * Direct libEGL.so.1 eglGetDisplay returns NO_DISPLAY for a plain client (the
 * screen server owns the GPU). HmiMain3D + the browser load libEGL_X.gpp.so /
 * libGLESv2_X_FORWARD.gpp.so (IMGegl* symbols) to get GL via the compositor.
 * This probe dlopens the forwarding EGL and reports whether it yields a usable
 * display + init - the decision point for the Quake3 render path. dlopen only,
 * so it links nothing GPU (libc has dlopen). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

typedef void* EGLDisplay; typedef unsigned EGLBoolean; typedef int EGLint;
#define EGL_NO_DISP ((void*)0)

static void* tryopen(const char* n){ void* h=dlopen(n,RTLD_LAZY|RTLD_GLOBAL); printf("  dlopen(%s) -> %s\n", n, h?"ok":dlerror()); return h; }

int main(void){
    printf("[FWD] probing forwarding EGL libs...\n");
    void* h = NULL;
    const char* cands[] = {
        "libEGL_X.gpp.so", "/opt/sys/lib/libEGL_X.gpp.so",
        "libEGL_X.cpp-ne.so", "/opt/sys/lib/libEGL_X.cpp-ne.so", NULL };
    int i; for(i=0; cands[i]; i++){ h = tryopen(cands[i]); if(h) break; }
    if(!h){ printf("[FWD] no forwarding EGL lib could be loaded\n"); return 1; }

    /* resolve GetDisplay/Initialize under both the IMGegl* and plain egl* names */
    EGLDisplay (*getdisp)(void*) = dlsym(h,"IMGeglGetDisplay");
    EGLBoolean (*initz)(EGLDisplay,EGLint*,EGLint*) = dlsym(h,"IMGeglInitialize");
    EGLint (*geterr)(void) = dlsym(h,"IMGeglGetError");
    const char* (*qstr)(EGLDisplay,EGLint) = dlsym(h,"IMGeglQueryString");
    if(!getdisp){ getdisp = dlsym(h,"eglGetDisplay"); initz = dlsym(h,"eglInitialize");
                  geterr = dlsym(h,"eglGetError"); qstr = dlsym(h,"eglQueryString");
                  printf("  (using plain egl* symbols)\n"); }
    else printf("  (using IMGegl* symbols)\n");
    if(!getdisp || !initz){ printf("[FWD] GetDisplay/Initialize symbols not found\n"); return 2; }

    intptr_t cand[] = { 0 /*DEFAULT*/, 1, 2 };
    for(i=0;i<3;i++){
        EGLDisplay d = getdisp((void*)cand[i]);
        printf("[FWD] GetDisplay(%ld) -> %p\n", (long)cand[i], d);
        if(d!=EGL_NO_DISP){
            EGLint ma=0,mi=0; EGLBoolean ok = initz(d,&ma,&mi);
            printf("       Initialize -> %u  EGL %d.%d  err=0x%x  vendor=%s\n",
                   ok, ma, mi, geterr?geterr():0, qstr?qstr(d,0x3053):"?");
            if(ok){ printf("[FWD] *** FORWARDING EGL GIVES A DISPLAY *** (display %ld) -> Quake3 render path viable here\n",(long)cand[i]); return 0; }
        }
    }
    printf("[FWD] forwarding EGL did not yield a usable display either\n");
    return 3;
}

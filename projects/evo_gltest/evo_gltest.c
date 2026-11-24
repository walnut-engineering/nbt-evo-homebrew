/* evo_gltest - minimal EGL + OpenGL ES 2.0 probe for the NBT EVO (OMAP5 / PowerVR SGX).
 *
 * The make-or-break test for 3D on this unit: open the DIRECT PowerVR EGL stack
 * (/lib/libEGL.so.1 + libGLESv2.so.1, NOT the HMI's forwarding libEGL_X), create a
 * QNX Screen window with OPENGL_ES2 usage, bring up a GLES2 context, render a
 * spinning clear + triangle, and measure frames/sec. If this runs at a high fps,
 * real 3D games (Quake 3 via a GLES renderer) are on the table.
 *
 * Every step logs so a failure shows exactly where EGL/Screen stops. Runs for a
 * fixed number of frames then tears down cleanly. iDrive BACK also quits (shared
 * feed), so it behaves as a launcher app once proven.
 *
 * Build: app.mk with GRAPHICS=0 + EXTRA_LIBS for EGL/GLESv2 (see Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <screen/screen.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static volatile int g_run = 1;
static void on_sig(int s){ (void)s; g_run = 0; }

static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0 + t.tv_nsec/1e6; }

#define CK(x) do{ if(!(x)){ printf("[GL] FAIL at: %s  (eglError=0x%x)\n", #x, eglGetError()); fflush(stdout); goto done; } }while(0)

static GLuint mkshader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s,1,&src,NULL);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[512]; glGetShaderInfoLog(s,sizeof log,NULL,log); printf("[GL] shader compile fail: %s\n", log); }
    return s;
}

int main(int argc, char** argv){
    int W = argc>1?atoi(argv[1]):640, H = argc>2?atoi(argv[2]):360;
    int FRAMES = argc>3?atoi(argv[3]):600;
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig); signal(SIGPIPE,SIG_IGN);
    printf("[GL] evo_gltest start: %dx%d, %d frames\n", W,H,FRAMES); fflush(stdout);

    screen_context_t sctx=0; screen_window_t win=0;
    EGLDisplay dpy=EGL_NO_DISPLAY; EGLSurface surf=EGL_NO_SURFACE; EGLContext ctx=EGL_NO_CONTEXT;

    /* ---- QNX Screen window (native window for EGL) ---- */
    CK(screen_create_context(&sctx, SCREEN_APPLICATION_CONTEXT)==0);
    CK(screen_create_window(&win, sctx)==0);
    int fmt = SCREEN_FORMAT_RGBX8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &fmt);
    int usage = SCREEN_USAGE_OPENGL_ES2;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    int size[2] = {W,H};
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SOURCE_SIZE, size);
    CK(screen_create_window_buffers(win, 2)==0);
    printf("[GL] Screen window created (%dx%d, OPENGL_ES2 usage)\n", W,H); fflush(stdout);

    /* ---- EGL bring-up (direct PowerVR) ----
     * QNX screen graphics.conf numbers the EGL displays: display 1 = PowerVR SGX,
     * display 2 = Vivante GC320. EGL_DEFAULT_DISPLAY (0) is NOT configured, so try
     * the numbered handles (1 = the 3D GPU we want) before the default. */
    { intptr_t cand[] = { 1, 2, (intptr_t)EGL_DEFAULT_DISPLAY };
      unsigned i; for (i=0;i<sizeof(cand)/sizeof(cand[0]);i++){
        dpy = eglGetDisplay((EGLNativeDisplayType)cand[i]);
        printf("[GL] eglGetDisplay(%ld) -> %p\n", (long)cand[i], (void*)dpy); fflush(stdout);
        if (dpy != EGL_NO_DISPLAY) break;
      } }
    CK(dpy != EGL_NO_DISPLAY);
    EGLint vmaj=0,vmin=0; CK(eglInitialize(dpy,&vmaj,&vmin));
    printf("[GL] EGL %d.%d  vendor=%s\n", vmaj,vmin, eglQueryString(dpy,EGL_VENDOR)); fflush(stdout);
    CK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint cfgattr[] = { EGL_SURFACE_TYPE,EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                         EGL_RED_SIZE,8, EGL_GREEN_SIZE,8, EGL_BLUE_SIZE,8, EGL_ALPHA_SIZE,0,
                         EGL_DEPTH_SIZE,16, EGL_NONE };
    EGLConfig cfg; EGLint ncfg=0;
    CK(eglChooseConfig(dpy,cfgattr,&cfg,1,&ncfg) && ncfg>=1);
    surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)win, NULL);
    CK(surf != EGL_NO_SURFACE);
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION,2, EGL_NONE };
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    CK(ctx != EGL_NO_CONTEXT);
    CK(eglMakeCurrent(dpy, surf, surf, ctx));
    printf("[GL] GLES2 context current. GL_RENDERER=%s  GL_VERSION=%s\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION)); fflush(stdout);

    /* ---- trivial GLES2 program: a triangle ---- */
    const char* vs = "attribute vec2 p; uniform float a;"
                     "void main(){ float c=cos(a),s=sin(a);"
                     "gl_Position=vec4(mat2(c,-s,s,c)*p,0.0,1.0); }";
    const char* fs = "precision mediump float; void main(){ gl_FragColor=vec4(1.0,0.6,0.1,1.0); }";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, mkshader(GL_VERTEX_SHADER,vs));
    glAttachShader(prog, mkshader(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(prog,0,"p");
    glLinkProgram(prog);
    GLint linked=0; glGetProgramiv(prog,GL_LINK_STATUS,&linked);
    if(!linked){ char log[512]; glGetProgramInfoLog(prog,sizeof log,NULL,log); printf("[GL] link fail: %s\n",log); }
    glUseProgram(prog);
    GLint ua = glGetUniformLocation(prog,"a");
    const GLfloat tri[] = { 0.0f,0.6f, -0.6f,-0.5f, 0.6f,-0.5f };
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,tri);
    glEnableVertexAttribArray(0);
    glViewport(0,0,W,H);

    /* ---- render loop + fps ---- */
    double t0 = now_ms(); int f;
    for(f=0; f<FRAMES && g_run; f++){
        float a = f*0.04f;
        glClearColor(0.05f,0.05f,0.12f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(ua, a);
        glDrawArrays(GL_TRIANGLES,0,3);
        eglSwapBuffers(dpy, surf);
    }
    { double dt = now_ms()-t0; double fps = f/(dt/1000.0);
      printf("[GL] RENDERED %d frames in %.1f ms = %.1f fps  (%dx%d)\n", f, dt, fps, W,H); fflush(stdout); }

done:
    if(dpy!=EGL_NO_DISPLAY){
        eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(ctx!=EGL_NO_CONTEXT) eglDestroyContext(dpy,ctx);
        if(surf!=EGL_NO_SURFACE) eglDestroySurface(dpy,surf);
        eglTerminate(dpy);
    }
    if(win) screen_destroy_window(win);
    if(sctx) screen_destroy_context(sctx);
    printf("[GL] done.\n"); fflush(stdout);
    return 0;
}

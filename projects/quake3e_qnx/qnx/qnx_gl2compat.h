/*
 * qnx_gl2compat.h - GL2 -> OpenGL ES 2.0 compatibility shim for the quake3e
 * renderer2 (rend2) on the NBT EVO head unit (QNX 6.5, PowerVR SGX544MP).
 *
 * renderer2/qgl.h includes this on __QNXNTO__ in place of the desktop
 * <GL/gl.h> + "glext.h". It pulls in the real GLES2 headers and back-fills the
 * desktop GL types and enum tokens that the rend2 source references but that
 * OpenGL ES 2.0 does not provide.
 *
 * Design: the renderer only ever calls GL through qgl* function pointers that
 * are resolved at runtime via ri.GL_GetProcAddress (-> dlsym into libGLESv2).
 * Every ES2-core name resolves to the real entry point. Desktop-only entry
 * points (fixed-function, glBegin/End, glOrtho, glDrawBuffer, glPolygonMode,
 * double-precision depth) are either never called on the paths we use (they
 * link as NULL pointers and the feature stays off) or are replaced by ES2
 * equivalents installed in the QNX glimp layer. This header's job is purely to
 * make the code COMPILE: provide the missing types and the enum tokens that
 * appear in switch/case labels, struct initialisers and function arguments.
 *
 * The enum VALUES below are the canonical Khronos constants. A token existing
 * here does NOT imply ES2 accepts it at runtime - e.g. the sized internal
 * formats (GL_RGBA8, GL_R32F, ...) are remapped to ES2-legal unsized formats in
 * the renderer's texture path; here they only need to be defined so the
 * internalFormat plumbing compiles.
 */
#ifndef QNX_GL2COMPAT_H
#define QNX_GL2COMPAT_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* qgl.h guards APIENTRY, but make it explicit for our proc typedefs. */
#ifndef APIENTRY
#define APIENTRY
#endif

/* ---- desktop scalar types ES2 lacks (used by qgl.h's DESKTOP_1_1 procs:
 *      glOrtho/glFrustum/glClipPlane/glDepthRange/glClearDepth) ---- */
typedef double GLdouble;
typedef double GLclampd;

/* =====================================================================
 * Core OpenGL 1.x tokens (normally from <GL/gl.h>) that ES2 dropped.
 * ===================================================================== */
#ifndef GL_SMOOTH
#define GL_SMOOTH               0x1D01
#endif
#ifndef GL_LINE
#define GL_LINE                 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL                 0x1B02
#endif
#ifndef GL_POLYGON
#define GL_POLYGON              0x0009
#endif
#ifndef GL_QUADS
#define GL_QUADS                0x0007
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT            0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT           0x0403
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX        0x1901
#endif

/* ---- sized internal formats (used as internalFormat; remapped at runtime) ---- */
#ifndef GL_RGB5
#define GL_RGB5                 0x8050
#endif
#ifndef GL_RGB8
#define GL_RGB8                 0x8051
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                0x8058
#endif
#ifndef GL_RGBA16
#define GL_RGBA16               0x805B
#endif
#ifndef GL_LUMINANCE8
#define GL_LUMINANCE8           0x8040
#endif
#ifndef GL_LUMINANCE8_ALPHA8
#define GL_LUMINANCE8_ALPHA8    0x8045
#endif
#ifndef GL_R32F
#define GL_R32F                 0x822E
#endif

/* =====================================================================
 * Tokens normally supplied by the desktop "glext.h" that rend2 references.
 * ===================================================================== */

/* depth / stencil renderbuffer formats */
#ifndef GL_DEPTH_COMPONENT16_ARB
#define GL_DEPTH_COMPONENT16_ARB   0x81A5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24       0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT24_ARB
#define GL_DEPTH_COMPONENT24_ARB   0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT32
#define GL_DEPTH_COMPONENT32       0x81A7
#endif
#ifndef GL_DEPTH_COMPONENT32_ARB
#define GL_DEPTH_COMPONENT32_ARB   0x81A7
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL           0x84F9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8        0x88F0
#endif
#ifndef GL_STENCIL_INDEX1
#define GL_STENCIL_INDEX1          0x8D46
#endif
#ifndef GL_STENCIL_INDEX4
#define GL_STENCIL_INDEX4          0x8D47
#endif
#ifndef GL_STENCIL_INDEX16
#define GL_STENCIL_INDEX16         0x8D49
#endif

/* framebuffer object (split read/draw targets are ES3/desktop; defined for
 * compile - the FBO blit path is gated off on ES2) */
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER        0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER        0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT   0x8CE0
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS   0x8CDF
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES             0x8D57
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER  0x8CDB
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER  0x8CDC
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE  0x8D56
#endif

/* occlusion queries (not on ES2; occlusionQuery stays off, defined for compile) */
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED          0x8914
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT            0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE  0x8867
#endif

/* depth-texture compare (shadow sampling); ES2 lacks it, pshadow path handles) */
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE    0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC    0x884D
#endif
#ifndef GL_COMPARE_R_TO_TEXTURE
#define GL_COMPARE_R_TO_TEXTURE    0x884E
#endif
#ifndef GL_DEPTH_TEXTURE_MODE
#define GL_DEPTH_TEXTURE_MODE      0x884B
#endif

/* 3D / cube-map texture tokens used in rend2 image code */
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R          0x8072
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB  0x8515
#endif

/* GL_MAX_VERTEX_UNIFORM_COMPONENTS (ES2 exposes _VECTORS instead; queried value
 * is handled at runtime) */
#ifndef GL_MAX_VERTEX_UNIFORM_COMPONENTS
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS  0x8B4A
#endif

/* GL_NUM_EXTENSIONS: GL 3.0 glGetStringi() extension enumeration. The code path
 * is gated behind qglGetStringi (NULL on ES2) so it never runs here; defined so
 * tr_init.c compiles. (On desktop this comes from <GL/gl.h>/glext; the Apple
 * branch of qgl.h also defines it, but that branch is inactive on QNX.) */
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS       0x821D
#endif

#endif /* QNX_GL2COMPAT_H */

/*
 * qnx_ilm.h - make our GLES2 Screen window visible over the live HMI on the
 * NBT EVO by registering it with the GENIVI LayerManager (ILM) and placing our
 * layer first in the CID screen's render order (= drawn on top).
 *
 * Confirmed against the live unit (LayerManagerControl): the CID is ILM screen
 * 0 (1440x540), and the FIRST layer in a screen's render order is composited on
 * top (the resident launcher layer 9001 sits first). We create our own layer,
 * attach our window's surface, and insert that layer first. We do NOT hide the
 * HMI layers (presentation mode) - an opaque full-screen window already covers
 * them, and not hiding avoids a black HMI if the game is killed uncleanly.
 */
#ifndef QNX_ILM_H
#define QNX_ILM_H

/* The ILM surface id our Screen window advertises via SCREEN_PROPERTY_ID_STRING.
 * The LayerManager creates an ILM surface with this id once the window is
 * posted. 2400/9400 are unused on the unit (launcher owns 2001/9001, its apps
 * 2000/9000). */
#define QNX_ILM_SURFACE_ID  2400
#define QNX_ILM_LAYER_ID    9400
#define QNX_ILM_SCREEN_ID   0       /* CID, 1440x540 */

/* Register surface QNX_ILM_SURFACE_ID on a new layer QNX_ILM_LAYER_ID and put
 * that layer on top of screen 0. Call AFTER the window has been posted at least
 * once (first eglSwapBuffers). Returns 0 on success. Safe no-op/!=0 if ILM is
 * unavailable. */
int  qnx_ilm_make_visible( int width, int height );

/* Remove our layer from the render order and tear down ILM (restores the HMI).
 * Call from GLimp_Shutdown. */
void qnx_ilm_hide( void );

#endif /* QNX_ILM_H */

#ifndef ILM_CLIENT_H
#define ILM_CLIENT_H

/*
 * ILM (Input Layer Management) Client API
 * Based on decompiled libilmClient.cpp-ne.so.1 from target system
 * 
 * Provides layer and surface management for QNX infotainment systems.
 * This API manages the composition and layering of visual elements.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Basic types */
typedef unsigned int t_ilm_uint;
typedef int t_ilm_int;
typedef unsigned char t_ilm_bool;
typedef t_ilm_uint t_ilm_surface;  // Surface ID is an unsigned int
typedef t_ilm_uint t_ilm_layer;    // Layer ID is an unsigned int
typedef unsigned int t_ilm_nativehandle;
typedef float t_ilm_float;

/* Return codes */
typedef enum {
    ILM_SUCCESS = 0,
    ILM_FAILED = -1,
    ILM_ERROR_INVALID_ARGUMENTS = -2,
    ILM_ERROR_RESOURCE_ALREADY_INUSE = -3,
    ILM_ERROR_RESOURCE_NOT_FOUND = -4,
    ILM_ERROR_NOT_IMPLEMENTED = -5,
    ILM_ERROR_ON_CONNECTION = -6,
    ILM_ERROR_INVALID_SURFACE_ID = -7,
    ILM_ERROR_INVALID_LAYER_ID = -8
} ilmErrorTypes;

// For compatibility, keep the old name as well
typedef ilmErrorTypes t_ilm_errorTypes;

/* Pixel format constants */
#define ILM_PIXELFORMAT_RGB_888       7     /* RGB 888 format */
#define ILM_PIXELFORMAT_RGBA_8888     1     /* RGBA 8888 format */
#define ILM_PIXELFORMAT_ARGB_8888     2     /* ARGB 8888 format */

/* Surface properties */
typedef struct {
    t_ilm_uint surfaceWidth;
    t_ilm_uint surfaceHeight;
    t_ilm_uint pixelformat;
    t_ilm_bool visibility;
    t_ilm_uint opacity;
    t_ilm_uint sourceX;
    t_ilm_uint sourceY;
    t_ilm_uint sourceWidth;
    t_ilm_uint sourceHeight;
    t_ilm_uint destX;
    t_ilm_uint destY;
    t_ilm_uint destWidth;
    t_ilm_uint destHeight;
} t_ilm_surfaceProperties;

/* Core ILM functions discovered from target library */

/**
 * Initialize the ILM client connection
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_init(void);

/**
 * Check if ILM is initialized
 * @return ILM_SUCCESS if initialized
 */
t_ilm_errorTypes ilm_isInitialized(void);

/**
 * Destroy the ILM client connection
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_destroy(void);

/**
 * Create a surface with given ID and native content
 * @param surfaceId unique surface identifier
 * @param nativehandle native window handle
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceCreate(t_ilm_uint surfaceId, t_ilm_nativehandle nativehandle);

/**
 * Remove a surface
 * @param surfaceId surface to remove
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceRemove(t_ilm_uint surfaceId);

/**
 * Get surface properties
 * @param surfaceId surface identifier
 * @param pSurfaceProperties pointer to properties structure
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_getPropertiesOfSurface(t_ilm_uint surfaceId, 
                                           t_ilm_surfaceProperties* pSurfaceProperties);

/**
 * Set surface viewport (destination rectangle)
 * @param surfaceId surface identifier
 * @param x destination X coordinate
 * @param y destination Y coordinate  
 * @param width destination width
 * @param height destination height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetViewport(t_ilm_uint surfaceId,
                                       t_ilm_uint x, t_ilm_uint y,
                                       t_ilm_uint width, t_ilm_uint height);

/**
 * Set surface destination rectangle (position and size on display)
 * @param surfaceId surface identifier
 * @param x destination X coordinate
 * @param y destination Y coordinate  
 * @param width destination width
 * @param height destination height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetDestinationRectangle(t_ilm_uint surfaceId, 
                                                   t_ilm_uint x, t_ilm_uint y,
                                                   t_ilm_uint width, t_ilm_uint height);

/**
 * Set surface source rectangle (portion of surface buffer to display)
 * @param surfaceId surface identifier
 * @param x source X coordinate
 * @param y source Y coordinate
 * @param width source width
 * @param height source height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetSourceRectangle(t_ilm_uint surfaceId,
                                              t_ilm_uint x, t_ilm_uint y,
                                              t_ilm_uint width, t_ilm_uint height);

/**
 * Get surface visibility
 * @param surfaceId surface identifier
 * @param pVisibility pointer to visibility flag
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceGetVisibility(t_ilm_uint surfaceId, t_ilm_bool* pVisibility);

/**
 * Get surface dimensions
 * @param surfaceId surface identifier
 * @param pWidth pointer to width
 * @param pHeight pointer to height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceGetDimension(t_ilm_uint surfaceId, 
                                        t_ilm_uint* pWidth, t_ilm_uint* pHeight);

/**
 * Set native content for surface
 * @param surfaceId surface identifier
 * @param nativehandle native window handle
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetNativeContent(t_ilm_uint surfaceId, t_ilm_nativehandle nativehandle);

/**
 * Remove native content from surface
 * @param surfaceId surface identifier
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceRemoveNativeContent(t_ilm_uint surfaceId);

/**
 * Create layer with specific dimensions
 * @param pLayerId pointer to layer identifier (input/output)
 * @param width layer width
 * @param height layer height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerCreateWithDimension(t_ilm_uint* pLayerId, t_ilm_uint width, t_ilm_uint height);

/**
 * Add surface to layer
 * @param layerId layer identifier
 * @param surfaceId surface identifier
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerAddSurface(t_ilm_uint layerId, t_ilm_uint surfaceId);

/**
 * Remove surface from layer
 * @param layerId layer identifier
 * @param surfaceId surface identifier
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerRemoveSurface(t_ilm_uint layerId, t_ilm_uint surfaceId);

/**
 * Get screen resolution
 * @param screenId screen identifier
 * @param pWidth pointer to width
 * @param pHeight pointer to height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_getScreenResolution(t_ilm_uint screenId, 
                                        t_ilm_uint* pWidth, t_ilm_uint* pHeight);

/**
 * Commit all changes to make them visible
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_commitChanges(void);

/**
 * Add notification callback for surface changes
 * @param surfaceId surface identifier
 * @param callback notification callback function
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceAddNotification(t_ilm_uint surfaceId, void* callback);

/**
 * Remove notification callback for surface
 * @param surfaceId surface identifier
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceRemoveNotification(t_ilm_uint surfaceId);

/**
 * Update input event acceptance
 * @param surfaceId surface identifier
 * @param inputDevice input device type
 * @param acceptance acceptance flag
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_UpdateInputEventAcceptanceOn(t_ilm_uint surfaceId, 
                                                  t_ilm_uint inputDevice,
                                                  t_ilm_bool acceptance);

/**
 * Remove synchronized surfaces
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_removeSynchronizedSurfaces(void);

/**
 * Set surface visibility
 * @param surfaceId surface identifier
 * @param visibility visibility flag (1=visible, 0=hidden)
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetVisibility(t_ilm_uint surfaceId, t_ilm_bool visibility);

/**
 * Set surface opacity
 * @param surfaceId surface identifier
 * @param opacity opacity value (0.0=transparent, 1.0=opaque)
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_surfaceSetOpacity(t_ilm_uint surfaceId, t_ilm_float opacity);

/**
 * Set layer destination rectangle (position and size on display)
 * @param layerId layer identifier
 * @param x destination X coordinate
 * @param y destination Y coordinate
 * @param width destination width  
 * @param height destination height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerSetDestinationRectangle(t_ilm_uint layerId,
                                                 t_ilm_uint x, t_ilm_uint y,
                                                 t_ilm_uint width, t_ilm_uint height);

/**
 * Set layer source rectangle
 * @param layerId layer identifier
 * @param x source X coordinate
 * @param y source Y coordinate
 * @param width source width
 * @param height source height
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerSetSourceRectangle(t_ilm_uint layerId,
                                           t_ilm_uint x, t_ilm_uint y,
                                           t_ilm_uint width, t_ilm_uint height);

/**
 * Set layer visibility
 * @param layerId layer identifier
 * @param visibility visibility flag (1=visible, 0=hidden)
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerSetVisibility(t_ilm_uint layerId, t_ilm_bool visibility);

/**
 * Set layer opacity
 * @param layerId layer identifier
 * @param opacity opacity value (0.0=transparent, 1.0=opaque)
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerSetOpacity(t_ilm_uint layerId, t_ilm_float opacity);

/**
 * Set display render order (Z-order of layers)
 * @param displayId display identifier (usually 0)
 * @param layerIds array of layer IDs in render order (front to back)
 * @param layerCount number of layers in array
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_displaySetRenderOrder(t_ilm_uint displayId, 
                                          t_ilm_uint* layerIds, 
                                          t_ilm_uint layerCount);

/**
 * Set layer render order (Z-order of surfaces within layer)
 * @param layerId layer identifier
 * @param surfaceIds array of surface IDs in render order
 * @param surfaceCount number of surfaces in array
 * @return ILM_SUCCESS on success
 */
t_ilm_errorTypes ilm_layerSetRenderOrder(t_ilm_uint layerId,
                                       t_ilm_uint* surfaceIds,
                                       t_ilm_uint surfaceCount);

/* Common surface IDs used in infotainment systems */
#define ILM_SURFACE_DOOM_GAME     666
#define ILM_SURFACE_HMI_BASE      2000
#define ILM_SURFACE_OVERLAY_BASE  3000

/* Common layer IDs */
#define ILM_LAYER_BACKGROUND      100
#define ILM_LAYER_APPLICATIONS    9999
#define ILM_LAYER_HMI            300
#define ILM_LAYER_OVERLAY        400

#ifdef __cplusplus
}
#endif

#endif /* ILM_CLIENT_H */

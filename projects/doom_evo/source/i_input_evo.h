#ifndef I_INPUT_QNX_H
#define I_INPUT_QNX_H

/*
 * QNX Input System Header for Automated Testing
 * 
 * This header provides the interface for the QNX input system
 * and includes compatibility definitions for DOOM event structures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

// DOOM compatibility - these are usually in doomdef.h, d_event.h, doomkeys.h
#ifndef DOOMDEF_H
// Basic DOOM types
typedef enum {
    false,
    true
} boolean;

typedef unsigned char byte;
#endif

#ifndef D_EVENT_H
// DOOM event types
typedef enum {
    ev_keydown,
    ev_keyup,
    ev_mouse
} evtype_t;

// DOOM event structure
typedef struct {
    evtype_t type;
    int data1;          // key scancode or mouse button
    int data2;          // mouse movement x
    int data3;          // mouse movement y
} event_t;
#endif

#ifndef DOOMKEYS_H
// DOOM key definitions
#define KEY_UPARROW     0x80
#define KEY_DOWNARROW   0x81
#define KEY_LEFTARROW   0x82
#define KEY_RIGHTARROW  0x83
#define KEY_ESCAPE      27
#define KEY_ENTER       13
#define KEY_SPACE       32
#define KEY_CTRL        (0x80 + 0x1d)
#define KEY_ALT         (0x80 + 0x38)
#define KEY_SHIFT       (0x80 + 0x2a)
#define KEY_F1          (0x80 + 0x3b)
#define KEY_F2          (0x80 + 0x3c)
#define KEY_F3          (0x80 + 0x3d)
#define KEY_F4          (0x80 + 0x3e)
#define KEY_F5          (0x80 + 0x3f)
#define KEY_F6          (0x80 + 0x40)
#define KEY_F7          (0x80 + 0x41)
#define KEY_F8          (0x80 + 0x42)
#define KEY_F9          (0x80 + 0x43)
#define KEY_F10         (0x80 + 0x44)
#define KEY_F11         (0x80 + 0x57)
#define KEY_F12         (0x80 + 0x58)
#endif

// Function prototypes for QNX input system (from i_input_qnx.c)
void I_InitDirectInput(void);
void I_ShutdownDirectInput(void);
void I_GetEvents(void);
void I_StartupMouse(void);
void I_StartupKeyboard(void);
void I_ShutdownKeyboard(void);
void I_ShutdownMouse(void);
void I_SetInputDebug(int enable);

// Simplified API for automated testing
int I_InitInput(void);
void I_ShutdownInput(void);
event_t* I_GetEvent(void);

// Testing utilities
void I_PostKeyEvent(int key, int pressed);
void I_PostMouseEvent(int button, int x, int y);
int I_GetEventQueueDepth(void);
int I_GetInputThreadStatus(void);

// Debug and monitoring functions
void I_PrintInputStats(void);
void I_ResetInputStats(void);
double I_GetAverageLatency(void);
double I_GetEventProcessingRate(void);
int I_GetTotalEventsProcessed(void);

#endif // I_INPUT_QNX_H

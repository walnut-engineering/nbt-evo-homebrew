/*
 * doomkeys.h - Key definitions for DOOM
 * 
 * This file contains key code definitions used throughout DOOM
 * for keyboard, mouse, and joystick input.
 */

#ifndef __DOOMKEYS_H__
#define __DOOMKEYS_H__

/* ASCII keys */
#define KEY_RIGHTARROW      0xae
#define KEY_LEFTARROW       0xac
#define KEY_UPARROW         0xad
#define KEY_DOWNARROW       0xaf
#define KEY_ESCAPE          27
#define KEY_ENTER           13
#define KEY_TAB             9
#define KEY_F1              (0x80+0x3b)
#define KEY_F2              (0x80+0x3c)
#define KEY_F3              (0x80+0x3d)
#define KEY_F4              (0x80+0x3e)
#define KEY_F5              (0x80+0x3f)
#define KEY_F6              (0x80+0x40)
#define KEY_F7              (0x80+0x41)
#define KEY_F8              (0x80+0x42)
#define KEY_F9              (0x80+0x43)
#define KEY_F10             (0x80+0x44)
#define KEY_F11             (0x80+0x57)
#define KEY_F12             (0x80+0x58)

#define KEY_BACKSPACE       127
#define KEY_PAUSE           0xff

#define KEY_EQUALS          0x3d
#define KEY_MINUS           0x2d

#define KEY_RSHIFT          (0x80+0x36)
#define KEY_RCTRL           (0x80+0x1d)
#define KEY_RALT            (0x80+0x38)

#define KEY_LALT            KEY_RALT

/* Mouse buttons */
#define KEY_MOUSE1          0x10c
#define KEY_MOUSE2          0x10d
#define KEY_MOUSE3          0x10e

/* Joystick buttons */
#define KEY_JOY1            0x200
#define KEY_JOY2            0x201
#define KEY_JOY3            0x202
#define KEY_JOY4            0x203

/* Additional keys */
#define KEY_SPACEBAR        ' '
#define KEY_STRAFE_L        ','
#define KEY_STRAFE_R        '.'

/* Action keys */
#define KEY_USE             'e'
#define KEY_FIRE            KEY_RCTRL

/* Screen API key codes */
#define KEYCODE_CTRL        0x1d
#define KEYCODE_LCTRL       0x1d
#define KEYCODE_RCTRL       0x1d
#define KEYCODE_ENTER       0x1c
#define KEYCODE_1           0x02
#define KEYCODE_2           0x03
#define KEYCODE_3           0x04
#define KEYCODE_4           0x05
#define KEYCODE_5           0x06
#define KEYCODE_6           0x07
#define KEYCODE_7           0x08
#define KEYCODE_8           0x09
#define KEYCODE_9           0x0a
#define KEYCODE_0           0x0b
#define KEYCODE_LSHIFT      0x2a
#define KEYCODE_RSHIFT      0x36
#define KEYCODE_LALT        0x38
#define KEYCODE_RALT        0x38
#define KEYCODE_EQUALS      0x0d

#endif /* __DOOMKEYS_H__ */

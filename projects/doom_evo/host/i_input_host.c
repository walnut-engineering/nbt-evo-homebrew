/*
 * i_input_host.c - scripted input for the HOST (x86) build of doom_evo.
 *
 * Replaces source/i_input_evo.c on the host. Instead of reading USB devices
 * it replays key events from a text file given with "-hostscript FILE".
 * Each line is:   <gametic> <key> <down|up|press>
 * where <key> is a single character ('w', 'a', ' ' is not allowed, use
 * SPACE), one of the names UP DOWN LEFT RIGHT ESC ENTER SPACE CTRL SHIFT ALT
 * TAB BACKSPACE F1..F12, or a decimal DOOM key code. "press" expands to a
 * key-down at <gametic> and a key-up two tics later. Lines starting with '#'
 * are ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "m_argv.h"

#define HOST_MAX_EVENTS 2048

typedef struct {
    int tic;
    int key;
    evtype_t type;
} host_event_t;

static host_event_t host_events[HOST_MAX_EVENTS];
static int host_nevents = 0;
static int host_next = 0;
static int host_initialized = 0;

static int host_key_from_name(const char *name)
{
    static const struct { const char *name; int key; } names[] = {
        { "UP", KEY_UPARROW }, { "DOWN", KEY_DOWNARROW },
        { "LEFT", KEY_LEFTARROW }, { "RIGHT", KEY_RIGHTARROW },
        { "ESC", KEY_ESCAPE }, { "ESCAPE", KEY_ESCAPE }, { "ENTER", KEY_ENTER },
        { "SPACE", ' ' }, { "CTRL", KEY_RCTRL }, { "SHIFT", KEY_RSHIFT },
        { "ALT", KEY_RALT }, { "TAB", KEY_TAB }, { "BACKSPACE", KEY_BACKSPACE },
        { "F1", KEY_F1 }, { "F2", KEY_F2 }, { "F3", KEY_F3 }, { "F4", KEY_F4 },
        { "F5", KEY_F5 }, { "F6", KEY_F6 }, { "F7", KEY_F7 }, { "F8", KEY_F8 },
        { "F9", KEY_F9 }, { "F10", KEY_F10 }, { "F11", KEY_F11 }, { "F12", KEY_F12 },
    };
    int i;

    if (strlen(name) == 1)
        return tolower((unsigned char)name[0]);
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (strcmp(names[i].name, name) == 0)
            return names[i].key;
    if (isdigit((unsigned char)name[0]))
        return atoi(name);
    return -1;
}

static void host_add_event(int tic, int key, evtype_t type)
{
    if (host_nevents >= HOST_MAX_EVENTS)
        return;
    host_events[host_nevents].tic = tic;
    host_events[host_nevents].key = key;
    host_events[host_nevents].type = type;
    host_nevents++;
}

static int host_event_cmp(const void *a, const void *b)
{
    return ((const host_event_t *)a)->tic - ((const host_event_t *)b)->tic;
}

static void host_load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int lineno = 0;

    if (!f) {
        printf("[HOST] input script %s not found, running without input\n", path);
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        int tic, key;
        char keyname[64], action[16];

        lineno++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%d %63s %15s", &tic, keyname, action) != 3) {
            printf("[HOST] %s:%d: cannot parse '%s'\n", path, lineno, line);
            continue;
        }
        key = host_key_from_name(keyname);
        if (key < 0) {
            printf("[HOST] %s:%d: unknown key '%s'\n", path, lineno, keyname);
            continue;
        }
        if (strcmp(action, "down") == 0) {
            host_add_event(tic, key, ev_keydown);
        } else if (strcmp(action, "up") == 0) {
            host_add_event(tic, key, ev_keyup);
        } else if (strcmp(action, "press") == 0) {
            host_add_event(tic, key, ev_keydown);
            host_add_event(tic + 2, key, ev_keyup);
        } else {
            printf("[HOST] %s:%d: unknown action '%s'\n", path, lineno, action);
        }
    }
    fclose(f);
    qsort(host_events, host_nevents, sizeof(host_events[0]), host_event_cmp);
    printf("[HOST] loaded %d scripted input events from %s\n", host_nevents, path);
}

void I_InitDirectInput(void)
{
    int p;

    if (host_initialized)
        return;
    host_initialized = 1;

    p = M_CheckParm("-hostscript");
    if (p && p < myargc - 1)
        host_load_script(myargv[p + 1]);
    else
        printf("[HOST] no -hostscript given, running without input\n");
    fflush(stdout);
}

void I_ShutdownDirectInput(void)
{
    host_initialized = 0;
}

void I_GetEvents(void)
{
    while (host_next < host_nevents && host_events[host_next].tic <= gametic) {
        event_t ev;

        ev.type = host_events[host_next].type;
        ev.data1 = host_events[host_next].key;
        ev.data2 = 0;
        ev.data3 = 0;
        D_PostEvent(&ev);
        printf("[HOST] tic %d: key %d %s\n", gametic, ev.data1,
               ev.type == ev_keydown ? "down" : "up");
        fflush(stdout);
        host_next++;
    }
}

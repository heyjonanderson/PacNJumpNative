/* A small stand-in for the parts of SDL3 that the shell (platform/sdl/main.c, menus.h, pack.h) uses, for the Nintendo DS on libnds.
 * Same idea as platform/dc/dc_sdl.h: the shell keeps its SDL names and this file supplies them. Drawing is in nds_gfx.h, sound in nds_audio.h.
 *
 * Two builds share it:
 *   the DS itself (devkitARM, libnds), and
 *   PNJ_NDS_HOST: the same shell and drawing code compiled for a PC with a software rasteriser in place of the DS's 3D hardware. It runs a scripted
 *   input, writes both screens to a picture and exits (tools/nds_hosttest.sh). It is how the DS code is checked without a DS.
 *
 * Included by main.c instead of <SDL3/SDL.h> when PNJ_NDS is defined. Only what the shell calls is here; it is not a general SDL. */
#ifndef PNJ_NDS_SDL_H
#define PNJ_NDS_SDL_H

#ifndef PNJ_NDS_HOST
#include <nds.h>
#include <fat.h>
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sys/stat.h>

#define SDLCALL
typedef uint8_t Uint8; typedef int8_t Sint8; typedef uint16_t Uint16; typedef int16_t Sint16;
typedef uint32_t Uint32; typedef int32_t Sint32; typedef uint64_t Uint64; typedef int64_t Sint64;
typedef Uint32 SDL_Keycode, SDL_PropertiesID, SDL_JoystickID, SDL_AudioDeviceID, SDL_WindowFlags, SDL_InitFlags;
typedef Uint64 SDL_FingerID, SDL_TouchID;
typedef int SDL_Scancode;
#define SDL_zero(x) memset(&(x), 0, sizeof (x))
#define SDL_free free
#define SDL_strcasecmp strcasecmp
#ifdef PNJ_NDS_HOST
#define SDL_Log(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#else
#define SDL_Log(...) ((void)0)
#endif
#define SDL_GetError() ""
#define SDL_ShowSimpleMessageBox(f, t, m, w) ((void)0)
#define SDL_MESSAGEBOX_INFORMATION 0
#define SDL_MESSAGEBOX_ERROR 1
#define SDL_SetHint(a, b) ((void)0)
#define SDL_HINT_ORIENTATIONS ""
#define SDL_HINT_ANDROID_TRAP_BACK_BUTTON ""
#define SDL_HINT_MOUSE_TOUCH_EVENTS ""
#define SDL_HINT_TOUCH_MOUSE_EVENTS ""
#define SDL_INIT_VIDEO 1
#define SDL_INIT_EVENTS 2
#define SDL_INIT_AUDIO 4
#define SDL_INIT_GAMEPAD 8
static bool nds_init(void);
#define SDL_Init(f) nds_init()
#define SDL_InitSubSystem(f) true
#define SDL_Quit() ((void)0)
#define SDL_WINDOW_FULLSCREEN 1

typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { int x, y, w, h; } SDL_Rect;
enum { SDL_BLENDMODE_BLEND = 1, SDL_BLENDMODE_ADD = 2 };
enum { SDL_SCALEMODE_NEAREST = 0, SDL_SCALEMODE_LINEAR = 1 };

/* ---------- time ---------- */
#ifdef PNJ_NDS_HOST
static Uint64 host_frames;                                   /* the host test has no clock: every drawn frame is 1/60 s */
static Uint64 SDL_GetPerformanceCounter(void) { return host_frames; }
static Uint64 SDL_GetPerformanceFrequency(void) { return 60; }
static Uint64 SDL_GetTicks(void) { return host_frames * 1000 / 60; }
#else
/* Hardware timers 0 and 1 chained count bus-clock ticks in 32 bits (about two minutes); the differences are summed into 64 bits. Called every frame. */
static Uint64 SDL_GetPerformanceCounter(void) { static u32 last; static Uint64 acc; u32 now = cpuGetTiming(); acc += (u32)(now - last); last = now; return acc; }
static Uint64 SDL_GetPerformanceFrequency(void) { return BUS_CLOCK; }
static Uint64 SDL_GetTicks(void) { return SDL_GetPerformanceCounter() * 1000ULL / BUS_CLOCK; }
#endif
#define SDL_Delay(ms) ((void)0)                              /* every frame already waits for the screen */

/* ---------- threads: the importer is not on this system ---------- */
typedef struct SDL_Thread SDL_Thread;
static void SDL_WaitThread(SDL_Thread *t, int *status) { (void)t; (void)status; }

/* ---------- events ---------- */
enum {
    SDL_EVENT_QUIT = 0x100, SDL_EVENT_WILL_ENTER_BACKGROUND = 0x102, SDL_EVENT_DID_ENTER_FOREGROUND = 0x104,
    SDL_EVENT_WINDOW_FIRST = 0x202, SDL_EVENT_WINDOW_MINIMIZED = 0x205, SDL_EVENT_WINDOW_RESTORED = 0x207,
    SDL_EVENT_WINDOW_FOCUS_GAINED = 0x20e, SDL_EVENT_WINDOW_FOCUS_LOST = 0x20f, SDL_EVENT_WINDOW_LAST = 0x21f,
    SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_KEY_UP = 0x301,
    SDL_EVENT_MOUSE_MOTION = 0x400, SDL_EVENT_MOUSE_BUTTON_DOWN = 0x401, SDL_EVENT_MOUSE_BUTTON_UP = 0x402,
    SDL_EVENT_GAMEPAD_AXIS_MOTION = 0x650, SDL_EVENT_GAMEPAD_BUTTON_DOWN = 0x651, SDL_EVENT_GAMEPAD_BUTTON_UP = 0x652, SDL_EVENT_GAMEPAD_ADDED = 0x653,
    SDL_EVENT_FINGER_DOWN = 0x700, SDL_EVENT_FINGER_UP = 0x701, SDL_EVENT_FINGER_MOTION = 0x702,
    SDL_EVENT_DROP_FILE = 0x1000
};
typedef struct { Uint32 type; SDL_Keycode key; Uint16 mod; bool down, repeat; } SDL_KeyboardEvent;
typedef struct { Uint32 type; SDL_JoystickID which; } SDL_GamepadDeviceEvent;
typedef struct { Uint32 type; SDL_JoystickID which; Uint8 button; bool down; } SDL_GamepadButtonEvent;
typedef struct { Uint32 type; SDL_JoystickID which; Uint8 axis; Sint16 value; } SDL_GamepadAxisEvent;
typedef struct { Uint32 type; SDL_TouchID touchID; SDL_FingerID fingerID; float x, y, dx, dy, pressure; } SDL_TouchFingerEvent;
typedef struct { Uint32 type; Uint32 which; float x, y; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; Uint32 which; Uint8 button; bool down; float x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; const char *data; } SDL_DropEvent;
typedef union SDL_Event {
    Uint32 type; SDL_KeyboardEvent key; SDL_GamepadDeviceEvent gdevice; SDL_GamepadButtonEvent gbutton; SDL_GamepadAxisEvent gaxis;
    SDL_TouchFingerEvent tfinger; SDL_MouseMotionEvent motion; SDL_MouseButtonEvent button; SDL_DropEvent drop; Uint8 pad[64];
} SDL_Event;
#define SDL_TOUCH_MOUSEID ((Uint32)-1)
typedef enum { SDL_TOUCH_DEVICE_INVALID = -1, SDL_TOUCH_DEVICE_DIRECT, SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE, SDL_TOUCH_DEVICE_INDIRECT_RELATIVE } SDL_TouchDeviceType;
#define SDL_GetTouchDeviceType(id) SDL_TOUCH_DEVICE_DIRECT
#define SDL_OpenGamepad(id) ((void)0)

#define NDS_NEVENTS 64
static SDL_Event nds_evq[NDS_NEVENTS]; static unsigned nds_evh, nds_evt;
static bool SDL_PushEvent(SDL_Event *e) { if (nds_evt - nds_evh >= NDS_NEVENTS) return false; nds_evq[nds_evt++ % NDS_NEVENTS] = *e; return true; }
static bool SDL_PollEvent(SDL_Event *e) { if (nds_evh == nds_evt) return false; *e = nds_evq[nds_evh++ % NDS_NEVENTS]; return true; }

/* ---------- keyboard: there is none, so every answer is "not pressed" ---------- */
enum {
    SDLK_RETURN = '\r', SDLK_ESCAPE = 0x1b, SDLK_SPACE = ' ', SDLK_1 = '1', SDLK_8 = '8',
    SDLK_A = 'a', SDLK_D = 'd', SDLK_P = 'p', SDLK_R = 'r', SDLK_S = 's', SDLK_W = 'w',
    SDLK_LEFT = 0x40000050, SDLK_RIGHT = 0x4000004f, SDLK_UP = 0x40000052, SDLK_DOWN = 0x40000051,
    SDLK_F11 = 0x40000044, SDLK_KP_ENTER = 0x40000058, SDLK_CAPSLOCK = 0x40000039, SDLK_SCROLLLOCK = 0x40000047, SDLK_NUMLOCKCLEAR = 0x40000053,
    SDLK_LCTRL = 0x400000e0, SDLK_RGUI = 0x400000e7, SDLK_AC_BACK = 0x40000118, SDLK_VOLUMEUP = 0x40000080, SDLK_VOLUMEDOWN = 0x40000081,
    SDLK_MUTE = 0x4000007f, SDLK_POWER = 0x40000066
};
#define SDL_KMOD_CTRL 0x00c0
#define SDL_KMOD_ALT 0x0300
#define SDL_KMOD_GUI 0xc000
#define SDL_SCANCODE_UNKNOWN 0
static const bool *SDL_GetKeyboardState(int *n) { static const bool none[512]; if (n) *n = 512; return none; }
#define SDL_GetScancodeFromKey(k, m) SDL_SCANCODE_UNKNOWN
#define SDL_GetKeyName(k) "KEY"

/* ---------- gamepad (SDL3 numbering, so saved bindings mean the same thing everywhere) ---------- */
enum {
    SDL_GAMEPAD_BUTTON_SOUTH = 0, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_COUNT = 26
};
enum { SDL_GAMEPAD_AXIS_LEFTX = 0, SDL_GAMEPAD_AXIS_LEFTY };

/* ---------- file streams (stdio underneath; libfat gives stdio the SD card) ---------- */
enum { SDL_IO_SEEK_SET = 0 };
typedef struct SDL_IOStream { FILE *f; } SDL_IOStream;
static SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode) {
    FILE *f = fopen(path, mode); if (!f) return NULL;
    SDL_IOStream *s = (SDL_IOStream *)calloc(1, sizeof *s); if (!s) { fclose(f); return NULL; }
    s->f = f; return s;
}
static Sint64 SDL_GetIOSize(SDL_IOStream *s) { long cur = ftell(s->f); fseek(s->f, 0, SEEK_END); long n = ftell(s->f); fseek(s->f, cur, SEEK_SET); return n; }
static size_t SDL_ReadIO(SDL_IOStream *s, void *buf, size_t n) { return fread(buf, 1, n, s->f); }
static Sint64 SDL_SeekIO(SDL_IOStream *s, Sint64 off, int whence) { (void)whence; return fseek(s->f, (long)off, SEEK_SET) == 0 ? off : -1; }
static bool SDL_CloseIO(SDL_IOStream *s) { fclose(s->f); free(s); return true; }
static bool SDL_ReadU16LE(SDL_IOStream *s, Uint16 *v) { unsigned char b[2]; if (SDL_ReadIO(s, b, 2) != 2) return false; *v = (Uint16)(b[0] | (b[1] << 8)); return true; }
static bool SDL_ReadU32LE(SDL_IOStream *s, Uint32 *v) { unsigned char b[4]; if (SDL_ReadIO(s, b, 4) != 4) return false; *v = (Uint32)b[0] | ((Uint32)b[1] << 8) | ((Uint32)b[2] << 16) | ((Uint32)b[3] << 24); return true; }

#include "nds_gfx.h"

/* ---------- start-up ---------- */
static int nds_fat_ok;
static bool nds_init(void) {
#ifndef PNJ_NDS_HOST
    defaultExceptionHandler();
    cpuStartTiming(0);
    nds_fat_ok = fatInitDefault() ? 1 : 0;
    soundEnable();
#else
    nds_fat_ok = 1;
#endif
    return true;
}

/* ---------- buttons and the touch screen -> gamepad and finger events ---------- */
static void pnj_push_button(int b, int down) {
    SDL_Event e; SDL_zero(e); e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.which = 1; e.gbutton.button = (Uint8)b; e.gbutton.down = down ? true : false; SDL_PushEvent(&e);
}
static void pnj_push_finger(Uint32 type, float nx, float ny) {
    SDL_Event e; SDL_zero(e); e.type = type; e.tfinger.touchID = 1; e.tfinger.fingerID = 1; e.tfinger.x = nx; e.tfinger.y = ny; SDL_PushEvent(&e);
}
enum { NK_A = 1, NK_B = 2, NK_SELECT = 4, NK_START = 8, NK_RIGHT = 16, NK_LEFT = 32, NK_UP = 64, NK_DOWN = 128, NK_R = 256, NK_L = 512, NK_X = 1024, NK_Y = 2048, NK_TOUCH = 4096 };   /* the DS's own key bits */
#ifdef PNJ_NDS_HOST
/* PNJ_NDS_KEYS is "frame:keys:tx:ty,frame:keys:tx:ty,...": from that frame on these keys are held (NK_* bits, decimal) and, with NK_TOUCH, the pen is at tx,ty */
static void host_keys(unsigned *held, int *tx, int *ty) {
    static const char *s; static int init; static unsigned cur; static int cx, cy, nextf = -1; static unsigned nk; static int nx, ny;
    if (!init) { init = 1; s = getenv("PNJ_NDS_KEYS"); if (s && !*s) s = NULL; if (s) { nextf = (int)strtol(s, (char **)&s, 10); nk = *s == ':' ? (unsigned)strtol(s + 1, (char **)&s, 10) : 0; nx = *s == ':' ? (int)strtol(s + 1, (char **)&s, 10) : 0; ny = *s == ':' ? (int)strtol(s + 1, (char **)&s, 10) : 0; } }
    while (nextf >= 0 && (Uint64)nextf <= host_frames) {
        cur = nk; cx = nx; cy = ny; nextf = -1;
        if (s && *s == ',') { s++; nextf = (int)strtol(s, (char **)&s, 10); nk = *s == ':' ? (unsigned)strtol(s + 1, (char **)&s, 10) : 0; nx = *s == ':' ? (int)strtol(s + 1, (char **)&s, 10) : 0; ny = *s == ':' ? (int)strtol(s + 1, (char **)&s, 10) : 0; }
    }
    *held = cur; *tx = cx; *ty = cy;
}
#endif
/* Once per loop. A confirms and B goes back, as on every DS game; Start pauses. The pen is one finger on the lower screen, 0..1 across it. */
static void pnj_nds_pump(void) {
    static const struct { unsigned k; int b; } map[] = {
        { NK_UP, SDL_GAMEPAD_BUTTON_DPAD_UP }, { NK_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN }, { NK_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { NK_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
        { NK_A, SDL_GAMEPAD_BUTTON_SOUTH }, { NK_B, SDL_GAMEPAD_BUTTON_EAST }, { NK_Y, SDL_GAMEPAD_BUTTON_WEST }, { NK_X, SDL_GAMEPAD_BUTTON_NORTH },
        { NK_START, SDL_GAMEPAD_BUTTON_START }, { NK_SELECT, SDL_GAMEPAD_BUTTON_BACK }, { NK_L, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { NK_R, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER } };
    static unsigned prev; static int pen;
    unsigned b; int tx = 0, ty = 0;
#ifdef PNJ_NDS_HOST
    host_keys(&b, &tx, &ty);
#else
    scanKeys(); b = keysHeld();
    if (b & KEY_TOUCH) { touchPosition tp; touchRead(&tp); tx = tp.px; ty = tp.py; }
#endif
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) { unsigned m = map[i].k; if ((b & m) != (prev & m)) pnj_push_button(map[i].b, (b & m) != 0); }
    prev = b;
    if (b & NK_TOUCH) { pnj_push_finger(pen ? SDL_EVENT_FINGER_MOTION : SDL_EVENT_FINGER_DOWN, tx / 256.0f, ty / 192.0f); pen = 1; }
    else if (pen) { pen = 0; pnj_push_finger(SDL_EVENT_FINGER_UP, 0, 0); }
}

#endif

/* Pac'N-Jump SDL3 shell: desktop and Android.
 * Renders the core's draw list with the original sprite atlas (assets/ folder, bundled into the APK by Gradle),
 * the scrolling background and the HUD. Menus, pause screen and audio are not in the native build yet.
 *
 * Controls
 *   touch: digital (default, like the original): hold the left or right half of the screen
 *          analog: drag a finger (slide bar at the bottom, or a floating stick anywhere above it)
 *   tap the HEIGHT label to switch digital/analog, tap the score digits to toggle "snappy" digital,
 *   tap the pause button (top centre) to pause/resume
 *   desktop: arrows/A/D, mouse drag = analog, R restart, P pause, Esc quit
 */
#ifdef PNJ_DC
#include "../dc/dc_sdl.h"            /* KallistiOS has no SDL3: a small stand-in for the calls used here, and the PVR drawing layer */
#elif defined(PNJ_NDS)
#include "../nds/nds_sdl.h"          /* Nintendo DS: the same kind of stand-in on libnds, drawing with the 3D hardware across both screens */
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif
#if defined(PNJ_DC) || defined(PNJ_NDS)
#define PNJ_NOIMPORT 1               /* no APK import on the console: the web builder makes the pack */
#define PNJ_SHIM 1                   /* drawing and events come from a stand-in for SDL (dc_sdl.h, nds_sdl.h) */
#endif
#ifdef PNJ_N3DS
#include "../n3ds/n3ds_gfx.h"        /* swaps SDL's window and renderer calls for citro2d; see that file */
#include <opendsp.h>                 /* open DSP firmware: ndsp works without a dumped dspfirm.cdc */
#endif
#ifndef PNJ_DEPTH
#define PNJ_DEPTH(r, z) ((void)0)       /* stereo depth is a 3DS thing; see n3ds_gfx.h */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef PNJ_NDS                      /* the DS never decodes a PNG: its textures are ready-made pages (nds_gfx.h) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#endif

#include "../../core/pnj_core.h"
#include "../../core/pnj_input.h"
#include "atlas.h"

static pnj_game G;
static pnj_input IN;
static int ctrl_analog = 0;     /* 0 digital (original default), 1 analog */
static int paused = 0;
static int touch_dir = 0;       /* digital touch: -1 left half, +1 right half */

/* ---------- assets ---------- */
static SDL_Texture *TEX[PNJ_NTEX];
#ifdef PNJ_N3DS
static SDL_Texture *FONTLITE, *FONTLITE8;
#endif
/* HDR output (desktop). The renderer is made with a linear output colorspace when the HDR option is on. SDL then scales ordinary (SDR) drawing to
 * the display's SDR white level by itself, so the picture looks the same as before, and colors above 1.0 are free to go brighter than white.
 * hdr_gain is how much brighter glow and sparkle sprites are pushed (1.0 = not at all). */
static int hdr_active; static float hdr_headroom = 1.0f, hdr_gain = 1.0f, hdr_play = 1.0f;   /* hdr_play: the gain used in gameplay; menus and the background get none */
static int hdrtest_mode;                                    /* --hdrtest: show the brightness patches instead of the game */
static int tex_sharp;                                      /* 1: nearest-neighbour scaling (Options > SCALE > PIXEL), 0: linear */
static int TEXW[PNJ_NTEX];

static void data_loaded(void);                          /* atlas and help text come from the pack; see below */
#include "pack.h"
#ifndef PNJ_NOIMPORT
#include "import.h"
#endif
static int no_bundle = 0;      /* PNJ_NO_BUNDLE=1: ignore loose/bundled files and use only the pack (testing the bring-your-own-APK path) */
static unsigned char *read_asset(const char *name, size_t *size) {
    if (PKN) {                                              /* the pack wins when present */
        char pn[96]; size_t l = strlen(name);
        if (l > 4 && !strcmp(name + l - 4, ".png") && !strchr(name, '/')) snprintf(pn, sizeof pn, "tex/%s", name); else snprintf(pn, sizeof pn, "%s", name);
        unsigned char *b = pack_read(pn, size); if (b) return b;
    }
    if (no_bundle) return 0;
    static const char *prefixes[] = {
#ifdef __ANDROID__
        "",
#else
        "assets/", "../assets/", "native/assets/",
#endif
        0 };
    char path[256];
    for (int i = 0; prefixes[i]; i++) {
        snprintf(path, sizeof path, "%s%s", prefixes[i], name);
        SDL_IOStream *rw = SDL_IOFromFile(path, "rb");
        if (!rw) continue;
        Sint64 n = SDL_GetIOSize(rw);
        unsigned char *buf = n > 0 ? (unsigned char *)malloc((size_t)n) : 0;
        if (buf && SDL_ReadIO(rw, buf, (size_t)n) == (size_t)n) { SDL_CloseIO(rw); *size = (size_t)n; return buf; }
        free(buf); SDL_CloseIO(rw);
    }
    return 0;
}

#ifdef PNJ_NDS
#include "../nds/nds_audio.h"
#else
#include "audio.h"
#endif

static SDL_Texture *get_tex(SDL_Renderer *r, int i) {
#ifdef PNJ_NDS
    (void)r; SDL_Texture *nt = nds_tex(i); TEXW[i] = nt->w; return nt;      /* a handle only: the pixels are looked up in the stage's page when it is drawn */
#else
    if (TEX[i]) return TEX[i];
    if (TEXW[i] < 0) return 0;                              /* failed before */
#ifdef PNJ_DC
    if (PKN) {                                              /* the pack's ready-made console texture: copy it into video memory, nothing to decode */
        char dn[96]; size_t dsz = 0; snprintf(dn, sizeof dn, "dc/%s.dtx", PNJ_TEXTURES[i]);
        unsigned char *d = pack_read(dn, &dsz);
        if (d && dsz >= 8 && (!memcmp(d, "DTX1", 4) || !memcmp(d, "DTX2", 4))) {
            int vq = d[3] == '2', dw = d[4] | (d[5] << 8), dh = d[6] | (d[7] << 8); SDL_Texture *t = NULL;
            size_t need = vq ? 8 + 2048 + (size_t)(dw / 2) * (dh / 2) : 8 + (size_t)dw * dh * 2;
            if (dsz >= need) {
                for (;;) {
                    t = vq ? pnj_tex_from_vq(d + 8, dw, dh, 0) : pnj_tex_from_4444((const uint16_t *)(d + 8), dw, dh, 0);
                    if (t || !dc_evict_lru(TEX, TEXW, PNJ_NTEX)) break;
                }
            }
            free(d);
            if (t) { TEX[i] = t; TEXW[i] = t->w; return t; }
        } else free(d);
    }
#endif
    char name[64]; snprintf(name, sizeof name, "%s.png", PNJ_TEXTURES[i]);
    size_t n; unsigned char *buf = read_asset(name, &n);
    int w = 0, h = 0, c = 0;
    unsigned char *px = buf ? stbi_load_from_memory(buf, (int)n, &w, &h, &c, 4) : 0;
    free(buf);
    if (!px) { TEXW[i] = -1; SDL_Log("missing texture %s", name); return 0; }
#if defined(PNJ_N3DS) || defined(PNJ_DC)
    int half = w >= 512;                                    /* the sheets are drawn at half their size; see n3ds_gfx.h */
    SDL_Texture *t = pnj_tex_make(px, w, h, half ? 2 : 1, 0);
#ifdef PNJ_DC
    while (!t && dc_evict_lru(TEX, TEXW, PNJ_NTEX)) t = pnj_tex_make(px, w, h, half ? 2 : 1, 0);      /* video memory full: drop the sheet drawn longest ago */
#endif
    if (t) w = t->w;
#ifdef PNJ_N3DS
    if (i == 2 && !FONTLITE && w == 512) {                  /* menus are drawn at half size: a pre-shrunk copy of the dotted font (16 px cells) stays readable */
        size_t rowb = (size_t)1024 * 4; unsigned char *fl = (unsigned char *)calloc(512, rowb);
        /* The shrunk copies sit in the corner of a full 512 x 512 texture: on a real console the small textures (256 x 128, 128 x 64) came out scrambled while
           the big sheets were fine. Create sdmc:/3ds/pacnjump/nolite to skip them and draw all text from the main sheet. */
        FILE *nl = fopen("sdmc:/3ds/pacnjump/nolite", "rb"); int skip = nl != NULL; if (nl) fclose(nl);
        if (fl && h >= 384 && !skip) { memcpy(fl, px, (size_t)384 * rowb); FONTLITE = pnj_tex_make_pad(fl, 1024, 512, 4, 0, 512, 512); FONTLITE8 = pnj_tex_make_pad(fl, 1024, 512, 8, 0, 512, 512); }
        free(fl);
    }
#endif
    (void)r;
#else
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) { SDL_UpdateTexture(t, NULL, px, w * 4); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, tex_sharp ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR); }
#endif
    stbi_image_free(px);
    TEX[i] = t; TEXW[i] = w;
    return t;
#endif
}

/* draw sprite id centred at (x,y); col/row/vrow pick animation cells like the web version */
static void spr(SDL_Renderer *r, int id, int col, int row, int vrow, double x, double y, double rot, double sc,
                double alpha, int additive, double cr, double cg, double cb) {
    if (id < 0 || id >= PNJ_NSPRITES) return;
    const pnj_sprite *s = spr_at(id);
    SDL_Texture *t = get_tex(r, s->tex);
    if (!t) return;
    double k = (double)TEXW[s->tex] / s->tw;
    int sx = s->x + col * (s->w + 2), sy = s->y + row * (s->h + 2) + vrow * s->h;
    SDL_FRect src = { (int)(sx * k + 0.5), (int)(sy * k + 0.5), (int)(s->w * k + 0.5), (int)(s->h * k + 0.5) };
    SDL_FRect dst = { (float)(x - s->w * sc / 2), (float)(y - s->h * sc / 2), (float)(s->w * sc), (float)(s->h * sc) };
    SDL_SetTextureBlendMode(t, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(t, (Uint8)(alpha < 0 ? 0 : alpha > 1 ? 255 : alpha * 255));
    if (additive && hdr_gain > 1.0f) SDL_SetTextureColorModFloat(t, (float)cr * hdr_gain, (float)cg * hdr_gain, (float)cb * hdr_gain);   /* glow and sparkle go past white on an HDR display */
    else SDL_SetTextureColorMod(t, (Uint8)(cr * 255), (Uint8)(cg * 255), (Uint8)(cb * 255));
    SDL_RenderTextureRotated(r, t, &src, &dst, rot, NULL, SDL_FLIP_NONE);
}
#define SPR(r, id, x, y) spr(r, id, 0, 0, 0, x, y, 0, 1, 1, 0, 1, 1, 1)
#define RADIANS(d) ((d) * 3.14159265358979323846 / 180.0)

/* ---------- background (orig C0091p): scrolls at 1/10 camera speed and pulses; stage 1 is additively tinted by tier ---------- */
static void draw_bg(SDL_Renderer *r) {
    int id = 128 + 3 * G.stage;
    const pnj_sprite *sp = spr_at(id);
    int hh = sp->h;
    double cam = (double)G.cam594;
    double s = 0.4 + sin(RADIANS((G.L % 30) * 6)) * 0.2;
    int y0 = cam < 0 ? ((int)floor(-cam / 10)) % hh : hh - ((int)floor(cam / 10)) % hh;
    double ys[2] = { y0, y0 - hh }; int n = y0 <= hh ? 2 : 1;
    double a = 1, cr = 1, cg = 1, cb = 1; int add = 0;
    if (G.stage == 0) {
        int tier = pnj_tier(&G), f5 = pnj_height(&G);
        double c[3];
        if (tier < 3)      { double r0 = f5 > 29000 ? (f5 - 29000) / 1000.0 * 0.3 : 0; c[0] = s; c[1] = s; c[2] = s + r0; }
        else if (tier < 6) { double r0 = f5 > 59000 ? (f5 - 59000) / 1000.0 * 0.3 : 0; c[0] = s + r0; c[1] = s + r0; c[2] = 0.3 + s - r0; }
        else if (tier < 9) { double r0 = f5 > 89000 ? (f5 - 89000) / 1000.0 * 0.3 : 0; c[0] = 0.3 + s; c[1] = 0.3 + s - r0; c[2] = s; }
        else               { c[0] = 0.3 + s; c[1] = s; c[2] = s; }
        double al = s;
        if (G.p.power) { c[0] = c[1] = c[2] = 0.8; al = 0.6; }
        cr = c[0] * al; cg = c[1] * al; cb = c[2] * al; add = 1;
        for (int i = 0; i < 3; i++) { double *p = i == 0 ? &cr : i == 1 ? &cg : &cb; if (*p > 1) *p = 1; if (*p < 0) *p = 0; }
    } else {
        a = (G.stage == 7 || G.stage == 5) ? 1 : s;
    }
    float g = hdr_gain; hdr_gain = 1.0f;                       /* the background keeps its normal brightness; only the foreground glow goes past white */
    { double hz = pnj_height(&G) / 90000.0; PNJ_DEPTH(r, -(0.5 + 0.5 * (hz > 1 ? 1 : hz))); }      /* far behind the screen, and further back the higher you climb */
    for (int i = 0; i < n; i++) spr(r, id, 0, 0, 0, 160, ys[i] + hh / 2.0, 0, 1, a, add, cr, cg, cb);
    PNJ_DEPTH(r, 0);
    hdr_gain = g;
}

/* ---------- HUD ---------- */
static int touch_ui_on(void);
static double draw_digits(SDL_Renderer *r, int v, double x, double y) {
    char buf[16]; snprintf(buf, sizeof buf, "%d", v < 0 ? 0 : v);
    for (char *p = buf; *p; p++) { SPR(r, 40 + (*p - '0'), x + 12, y + 16); x += 20; }
    return x;
}
static void draw_hud(SDL_Renderer *r) {
    SPR(r, 58, 44, 24);
    double ex = draw_digits(r, pnj_height(&G), 0, 35);
    SPR(r, 59, ex + 2 + 26, 42 + 12.5);
    int tui = touch_ui_on();
#ifndef PNJ_N3DS
    if (tui) SPR(r, 65, 160, 19);                          /* pause button (the tap area stays live when hidden); the 3DS pauses with Start only */
#endif
    if (!tui) return;
    if (ctrl_analog) {
        spr(r, 75, 0, 0, 0, 160, 432, 0, 1, 0.4, 1, 1, 0.9, 0.4);
        double kx = 160, ky = 432;
        if (IN.active) {
            double dx = IN.x - IN.ax, dy = IN.y - IN.ay, d = sqrt(dx * dx + dy * dy);
            if (d >= 90) { dx = dx / d * 90; dy = dy / d * 90; }
            if (IN.bar) kx = IN.x < 60 ? 60 : (IN.x > 260 ? 260 : IN.x);
            else { kx = 160 + dx; spr(r, 76, 0, 0, 0, IN.ax + dx, IN.ay + dy, 0, 1, 0.4, 1, 1, 1, 1); }
        }
        if (IN.active) spr(r, 76, 0, 0, 0, kx, ky, 0, 1, 0.9, 1, 1, 0.6, 0.4);
        else spr(r, 76, 0, 0, 0, kx, ky, 0, 1, 0.9, 1, 1, 0.8, 0.4);
    } else {
        int l = IN.left || touch_dir < 0, rr = IN.right || touch_dir > 0;
        spr(r, 77, 0, 0, 0, 70, 430, 0, 1.2, 0.8, 1, l ? 1 : 0.7, l ? 1 : 0.7, l ? 0.7 : 1);
        spr(r, 78, 0, 0, 0, 250, 430, 0, 1.2, 0.8, 1, rr ? 1 : 0.7, rr ? 1 : 0.7, rr ? 0.7 : 1);
    }
}

/* ---------- game data (bring-your-own-APK) ---------- */
static int need_data = 0;
static char imp_msg[300];
static void reset_assets(void) {                      /* after a new pack arrives: drop cached textures, reload sounds */
    for (int i = 0; i < PNJ_NTEX; i++) { if (TEX[i]) SDL_DestroyTexture(TEX[i]); TEX[i] = 0; TEXW[i] = 0; }
    if (aud_dev) { mus_want[0] = 0; audio_music_flush(); audio_load_all(); }
}
#ifdef PNJ_NDS
static int data_present(void) { return pack_has("nds/stage1.ndp"); }
#else
static int data_present(void) { size_t n; unsigned char *b = read_asset("sysparts.png", &n); free(b); return b != NULL; }
#endif
#ifndef PNJ_NOIMPORT
static int do_import(const char *apk, const char *outpath) {
    char out[620]; char err[240];
    if (outpath) snprintf(out, sizeof out, "%s", outpath);
    else { if (!data_dir()[0]) { snprintf(imp_msg, sizeof imp_msg, "no writable data folder"); return 0; } data_path(out, sizeof out, "pnj.pack"); }
    if (!import_apk(apk, out, err, sizeof err)) { snprintf(imp_msg, sizeof imp_msg, "Import failed: %.200s", err); return 0; }
    if (!pack_open(out)) { snprintf(imp_msg, sizeof imp_msg, "Import wrote %s but it could not be opened", out); return 0; }
    pack_load_maps(); data_loaded(); need_data = 0;
    snprintf(imp_msg, sizeof imp_msg, "Imported %d files from your APK.", PKN);
    return 1;
}
#endif
static int ends_apk(const char *s) { size_t l = strlen(s); return l > 4 && !SDL_strcasecmp(s + l - 4, ".apk"); }
#if defined(__ANDROID__) || defined(__vita__) || defined(PNJ_N3DS)
#include <dirent.h>
static int import_start(const char *apk, const char *outpath, int rm);
static void scan_for_apk(void) {                      /* Android and Vita: pick up any .apk copied into the app's own folder (each file is tried once) */
    static char tried[700], vdir[700];
#ifdef __ANDROID__
    const char *dir = SDL_GetAndroidExternalStoragePath(); if (!dir) return;
#else
    snprintf(vdir, sizeof vdir, "%s", data_dir()); size_t vl = strlen(vdir); if (vl > 1 && vdir[vl - 1] == '/') vdir[vl - 1] = 0;
    const char *dir = vdir; if (!dir[0]) return;
#endif
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e; char p[700];
    while ((e = readdir(d))) if (ends_apk(e->d_name)) {
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!strcmp(p, tried)) continue;
        closedir(d); snprintf(tried, sizeof tried, "%s", p); import_start(p, NULL, 0); return;
    }
    closedir(d);
}
#endif
#ifndef PNJ_VERSION
#define PNJ_VERSION "dev"
#endif
#ifndef __ANDROID__
#if !defined(__vita__) && !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
static const char *DATA_HELP = "Pac'N-Jump needs the game data from your own copy of the original Android APK.\n\nDrag the APK onto this window, or run:  pnj --import game.apk";
#endif
#endif

/* ---------- import on a worker thread, with a loader screen ---------- */
/* The APK is unpacked and the pack written on a thread so the window keeps drawing. A tiny built-in 5x7 font draws the loader because the game's own
 * font is inside the data being imported. */
static const struct { char c; unsigned char r[7]; } TF[] = {
    {'A',{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}}, {'B',{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}}, {'C',{0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}}, {'D',{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}},
    {'E',{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}}, {'F',{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}}, {'G',{0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}}, {'H',{0x11,0x11,0x11,0x1f,0x11,0x11,0x11}},
    {'I',{0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0c}}, {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1f}},
    {'M',{0x11,0x1b,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}}, {'O',{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}}, {'P',{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}},
    {'Q',{0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}}, {'R',{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}}, {'S',{0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}}, {'T',{0x1f,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0e}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0a,0x04}}, {'W',{0x11,0x11,0x11,0x15,0x15,0x1b,0x11}}, {'X',{0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}},
    {'Y',{0x11,0x11,0x0a,0x04,0x04,0x04,0x04}}, {'Z',{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}}, {'0',{0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}}, {'1',{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}},
    {'2',{0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}}, {'3',{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}}, {'4',{0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}}, {'5',{0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}},
    {'6',{0x06,0x08,0x10,0x1e,0x11,0x11,0x0e}}, {'7',{0x1f,0x01,0x02,0x04,0x08,0x08,0x08}}, {'8',{0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}}, {'9',{0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c}},
    {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}}, {'.',{0x00,0x00,0x00,0x00,0x00,0x0c,0x0c}}, {'-',{0x00,0x00,0x00,0x1f,0x00,0x00,0x00}}, {':',{0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00}},
    {'!',{0x04,0x04,0x04,0x04,0x04,0x00,0x04}}, {'\'',{0x04,0x04,0x08,0x00,0x00,0x00,0x00}}, {',',{0x00,0x00,0x00,0x00,0x0c,0x04,0x08}}, {'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}} };
static void tf_text(SDL_Renderer *r, const char *s, double cx, double y, int sc, Uint8 R, Uint8 G, Uint8 B) {
    int n = (int)strlen(s); int x = (int)floor(cx - (n * 6 - 1) * sc / 2.0 + 0.5), yy = (int)floor(y + 0.5);   /* whole pixels only: a half-pixel start made the 3DS GPU round the dots unevenly and garble the letters */
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (; *s; s++, x += 6 * sc) {
        for (size_t g = 0; g < sizeof TF / sizeof *TF; g++) if (TF[g].c == *s)
            for (int ry = 0; ry < 7; ry++) for (int rx = 0; rx < 5; rx++) if (TF[g].r[ry] >> (4 - rx) & 1) {
                int run = 1; while (rx + run < 5 && (TF[g].r[ry] >> (4 - rx - run) & 1)) run++;      /* one rectangle per horizontal run: the 3DS can only draw so many per frame */
                SDL_FRect q = { (float)(x + rx * sc), (float)(yy + ry * sc), (float)(sc * run), (float)sc }; SDL_RenderFillRect(r, &q); rx += run - 1;
            }
    }
}
static float imp_shown;                                     /* the loader bar's own position, permille: it eases toward the real progress and creeps a little ahead of it, so it never looks stuck */
static Uint64 imp_t0;
#ifndef PNJ_NOIMPORT
static struct { SDL_Thread *th; volatile int busy, done, ok; char apk[1100], out[700], err[240]; int rm; } IJ;
static int import_thread(void *p) { (void)p; IJ.ok = import_apk(IJ.apk, IJ.out, IJ.err, sizeof IJ.err); IJ.done = 1; return 0; }
/* returns 0 (with imp_msg set) if it could not start. rm: delete the apk afterwards (Android hands us a temporary copy) */
#ifdef PNJ_N3DS
#define PNJ_IMPORT_STACK (512 * 1024)       /* the 3DS has far less memory than the Vita */
#else
#define PNJ_IMPORT_STACK (1024 * 1024)
#endif
static int import_start(const char *apk, const char *outpath, int rm) {
    if (IJ.busy) return 0;
    if (outpath) snprintf(IJ.out, sizeof IJ.out, "%s", outpath);
    else { if (!data_dir()[0]) { snprintf(imp_msg, sizeof imp_msg, "no writable data folder"); return 0; } data_path(IJ.out, sizeof IJ.out, "pnj.pack"); }
    snprintf(IJ.apk, sizeof IJ.apk, "%s", apk); IJ.rm = rm; IJ.done = 0; IJ.ok = 0; IJ.err[0] = 0; imp_prog(0, IS_OPEN); imp_shown = 0; imp_t0 = SDL_GetTicks();
#if defined(__vita__) || defined(PNJ_N3DS)
    { SDL_PropertiesID tp = SDL_CreateProperties();                   /* the console default thread stacks are small */
      SDL_SetPointerProperty(tp, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)import_thread);
      SDL_SetStringProperty(tp, SDL_PROP_THREAD_CREATE_NAME_STRING, "import");
      SDL_SetNumberProperty(tp, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, PNJ_IMPORT_STACK);
      IJ.th = SDL_CreateThreadWithProperties(tp); SDL_DestroyProperties(tp); }
#else
    IJ.th = SDL_CreateThread(import_thread, "import", NULL);
#endif
    if (!IJ.th) { snprintf(imp_msg, sizeof imp_msg, "could not start the import thread"); return 0; }
    IJ.busy = 1; return 1;
}
/* call on the main thread once IJ.done: loads the new pack and sets imp_msg; returns 1 on success */
static int import_finish(void) {
    SDL_WaitThread(IJ.th, NULL); IJ.th = NULL; IJ.busy = 0;
    if (IJ.rm) remove(IJ.apk);
    if (!IJ.ok) { snprintf(imp_msg, sizeof imp_msg, "Import failed: %.200s", IJ.err); return 0; }
    if (!pack_open(IJ.out)) { snprintf(imp_msg, sizeof imp_msg, "Import wrote %s but it could not be opened", IJ.out); return 0; }
    pack_load_maps(); data_loaded(); need_data = 0;
    snprintf(imp_msg, sizeof imp_msg, "Imported %d files from your APK.", PKN);
    return 1;
}
static const char *imp_stage_name(int s) {
    static const char *const n[] = { "OPENING APK", "UNPACKING FILES", "READING SPRITE TABLE", "CONVERTING AUDIO", "WRITING GAME DATA", "DONE" };
    return n[s < 0 || s > IS_DONE ? 0 : s];
}
static void draw_import(SDL_Renderer *r) {                  /* 320x480 field coordinates */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
    int pm = imp_pm < 0 ? 0 : imp_pm > 1000 ? 1000 : imp_pm; char b[24];
#ifdef PNJ_N3DS
    if (r->cur == 0 && r->eye <= 0)                         /* once per frame: the right eye and the touch screen redraw the same state */
#endif
    {
        imp_shown += ((float)pm - imp_shown) * 0.10f;
        if (imp_shown < (float)pm + 12.0f && pm < 1000) imp_shown += 0.25f;
        if (imp_shown > 1000.0f) imp_shown = 1000.0f;
        if (pm >= 1000) imp_shown = 1000.0f;
    }
    int sh = (int)imp_shown;
#ifdef PNJ_N3DS
    /* Top screen: the title, the bar, the percentage and the step. Touch screen: the elapsed time and the notes, in larger type (field rows 240..479). */
    char b2[16]; { unsigned sec = (unsigned)((SDL_GetTicks() - imp_t0) / 1000); snprintf(b2, sizeof b2, "%u:%02u", sec / 60, sec % 60); }
    if (r->cur != 0) {
        tf_text(r, b2, 160, 240 + 20, 3, 255, 255, 255);
        tf_text(r, "THIS CAN TAKE", 160, 240 + 70, 2, 255, 224, 122);
        tf_text(r, "A FEW MINUTES.", 160, 240 + 92, 2, 255, 224, 122);
        tf_text(r, "IT ONLY HAPPENS ONCE.", 160, 240 + 128, 2, 255, 255, 255);
        tf_text(r, "KEEP THE POWER ON AND", 160, 240 + 154, 2, 200, 200, 220);
        tf_text(r, "THE SD CARD IN.", 160, 240 + 176, 2, 200, 200, 220);
        return;
    }
    tf_text(r, "IMPORTING GAME DATA", 160, 16, 3, 255, 224, 122);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255); SDL_FRect o = { 20, 62, 280, 24 }; SDL_RenderRect(r, &o);
    SDL_SetRenderDrawColor(r, 255, 224, 122, 255); SDL_FRect f = { 23, 65, (float)((274 * sh) / 1000), 18 }; SDL_RenderFillRect(r, &f);
    snprintf(b, sizeof b, "%d%%", sh / 10);
    tf_text(r, b, 160, 106, 4, 255, 255, 255);
    tf_text(r, imp_stage_name(imp_stage), 160, 160, 2, 170, 170, 190);
#else
    tf_text(r, "IMPORTING", 160, 110, 4, 255, 224, 122);
    tf_text(r, "GAME DATA", 160, 150, 4, 255, 224, 122);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255); SDL_FRect o = { 40, 232, 240, 22 }; SDL_RenderRect(r, &o);
    SDL_SetRenderDrawColor(r, 255, 224, 122, 255); SDL_FRect f = { 43, 235, (float)((234 * sh) / 1000), 16 }; SDL_RenderFillRect(r, &f);
    snprintf(b, sizeof b, "%d%%", sh / 10);
    tf_text(r, b, 160, 268, 3, 255, 255, 255);
    tf_text(r, imp_stage_name(imp_stage), 160, 304, 2, 170, 170, 190);
    { unsigned sec = (unsigned)((SDL_GetTicks() - imp_t0) / 1000); snprintf(b, sizeof b, "%u:%02u", sec / 60, sec % 60); tf_text(r, b, 160, 336, 2, 120, 120, 140); }
#ifdef __vita__
    tf_text(r, "THE FIRST LOAD CAN TAKE", 160, 384, 2, 255, 255, 255);
    tf_text(r, "UP TO A MINUTE", 160, 410, 2, 255, 255, 255);
    tf_text(r, "IT ONLY HAPPENS ONCE", 160, 446, 1, 200, 200, 200);
#endif
#endif
}
#else
/* no importer here: the same names, doing nothing, so the main loop below stays one piece */
static struct { SDL_Thread *th; volatile int busy, done; int rm; char apk[4]; } IJ;
static int imp_pm, imp_stage;
static int import_start(const char *apk, const char *outpath, int rm) { (void)apk; (void)outpath; (void)rm; snprintf(imp_msg, sizeof imp_msg, "no import on this system"); return 0; }
static int import_finish(void) { return 0; }
static const char *imp_stage_name(int s) { (void)s; return ""; }
static void draw_import(SDL_Renderer *r) { (void)r; }
#endif
#ifndef __ANDROID__
static void draw_drop_hint(SDL_Renderer *r) {                /* no game data yet */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
#ifdef PNJ_DC
    tf_text(r, "GAME DATA NOT FOUND", 160, 150, 2, 255, 224, 122);
    tf_text(r, "PNJ.PACK BELONGS ON", 160, 190, 2, 255, 224, 122);
    tf_text(r, "THE DISC OR THE SD CARD", 160, 216, 2, 255, 224, 122);
    tf_text(r, "THE WEB BUILDER MAKES IT FROM", 160, 280, 1, 170, 170, 190);
    tf_text(r, "YOUR OWN ANDROID APK", 160, 300, 1, 170, 170, 190);
    return;
#endif
#ifdef PNJ_NDS
    tf_text(r, "GAME DATA NOT FOUND", 160, 60, 2, 255, 224, 122);
    tf_text(r, "COPY PNJ_DS.PACK TO THE", 160, 110, 2, 255, 255, 255);
    tf_text(r, "PACNJUMP FOLDER ON THE", 160, 136, 2, 255, 255, 255);
    tf_text(r, "SD CARD", 160, 162, 2, 255, 255, 255);
    tf_text(r, nds_fat_ok ? "SD CARD: OK" : "SD CARD: NOT FOUND", 160, 270, 2, 170, 170, 190);
    tf_text(r, "MAKE THE FILE FROM YOUR OWN", 160, 330, 2, 170, 170, 190);
    tf_text(r, "ANDROID APK ON A PC", 160, 356, 2, 170, 170, 190);
    return;
#endif
#if defined(__vita__) || defined(PNJ_N3DS)
    { char path[160]; snprintf(path, sizeof path, "%s", data_dir());
      for (char *c = path; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
      tf_text(r, "COPY YOUR PAC'N-JUMP", 160, 150, 2, 255, 224, 122);
      tf_text(r, "APK INTO THIS FOLDER", 160, 176, 2, 255, 224, 122);
#ifdef PNJ_N3DS
      tf_text(r, "ON THE SD CARD", 160, 202, 2, 255, 224, 122);
#else
      tf_text(r, "ON THE VITA", 160, 202, 2, 255, 224, 122);
#endif
#ifdef PNJ_N3DS
      tf_text(r, path, 160, 262, 2, 255, 255, 255);
#else
      tf_text(r, path, 160, 262, 1, 255, 255, 255);
#endif
      tf_text(r, "THE ORIGINAL ANDROID FILE", 160, 320, 1, 170, 170, 190);
      tf_text(r, "THE GAME FINDS IT BY ITSELF", 160, 340, 1, 170, 170, 190); }
    return;
#endif
    tf_text(r, "DROP YOUR", 160, 170, 3, 255, 224, 122);
    tf_text(r, "PAC'N-JUMP APK", 160, 204, 3, 255, 224, 122);
    tf_text(r, "ON THIS WINDOW", 160, 238, 3, 255, 224, 122);
    tf_text(r, "THE ORIGINAL ANDROID FILE", 160, 300, 1, 170, 170, 190);
}
#endif

/* Mouse and finger positions. SDL3 hands us window coordinates (fingers are 0..1 across the window) and leaves the conversion to the renderer's
 * logical presentation to us, so every position goes through SDL_RenderCoordinatesFromWindow, or through tate_map when the picture is turned. */
/* ---------- frame ---------- */
#include "menus.h"

#if !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
/* ---------- renderer and HDR state ---------- */
static int hdr_possible = 1;                                /* 0 once a renderer would not give us linear output (no HDR backend on this system) */
static SDL_Renderer *make_renderer(SDL_Window *win, int want_hdr) {
    SDL_Renderer *r = NULL;
    if (want_hdr && hdr_possible) {
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, win);
        SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB_LINEAR);
        SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1);
        r = SDL_CreateRendererWithProperties(p); SDL_DestroyProperties(p);
        if (r && SDL_GetNumberProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB) != SDL_COLORSPACE_SRGB_LINEAR) {
            SDL_Log("hdr: renderer '%s' has no linear output, using SDR", SDL_GetRendererName(r)); SDL_DestroyRenderer(r); r = NULL; hdr_possible = 0;
        } else if (!r) { SDL_Log("hdr: could not make an HDR renderer: %s", SDL_GetError()); hdr_possible = 0; }
    }
    if (!r) { r = SDL_CreateRenderer(win, NULL); if (r) SDL_SetRenderVSync(r, 1); }
    if (!r) r = SDL_CreateRenderer(win, "software");
    if (r) SDL_Log("renderer: %s%s", SDL_GetRendererName(r), want_hdr && hdr_possible ? " (linear output, HDR requested)" : "");
    return r;
}
/* every frame: is the display in HDR mode right now, and how far above SDR white does it go? */
static void hdr_poll(SDL_Renderer *r) {
    SDL_PropertiesID p = SDL_GetRendererProperties(r);
    int on = SV.hdr && SDL_GetBooleanProperty(p, SDL_PROP_RENDERER_HDR_ENABLED_BOOLEAN, false);
    hdr_headroom = on ? SDL_GetFloatProperty(p, SDL_PROP_RENDERER_HDR_HEADROOM_FLOAT, 1.0f) : 1.0f;
    hdr_active = on && hdr_headroom > 1.0f;
    hdr_play = hdr_active ? 1.0f + (fminf(hdr_headroom, 4.0f) - 1.0f) * 0.85f : 1.0f;
    hdr_gain = hdr_play;
}
/* --hdrtest: a row of white patches at 1x, 1.5x, 2x, 3x and 4x SDR white. On an HDR display the patches step up in brightness until the display runs out. */
static void draw_hdrtest(SDL_Renderer *r) {
    static const float lv[] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    SDL_SetRenderDrawColorFloat(r, 0.04f, 0.04f, 0.04f, 1.0f); SDL_RenderClear(r);
    for (int i = 0; i < 5; i++) {
        float v = lv[i];
        SDL_SetRenderDrawColorFloat(r, v, v, v, 1.0f);
        SDL_FRect q = { 30.0f, 40.0f + i * 70.0f, 260.0f, 56.0f }; SDL_RenderFillRect(r, &q);
        char b[24]; snprintf(b, sizeof b, "%.1fX", (double)lv[i]);
        tf_text(r, b, 160, 60.0 + i * 70.0, 3, 20, 20, 20);
    }
    char b[48]; snprintf(b, sizeof b, hdr_active ? "HDR ON PEAK %.1fX" : "SDR", (double)hdr_headroom);
    tf_text(r, b, 160, 410, 2, 255, 224, 122);
}

/* ---------- screen orientation (tate) ----------
 * Standard: the 320x480 field is letterboxed in the window by SDL's logical size, and SDL turns mouse and finger positions into field
 * coordinates before we see them. Tate left / right: the field is drawn into a texture at its on-screen size, turned a quarter turn and
 * centred in the window; there is no logical size, so positions arrive as plain window positions and are turned back here. */
static SDL_Texture *tgt; static int tgt_hdr, tgt_w, tgt_h, t_cx, t_cy, applied_orient = -1, applied_scale = -1, applied_int = -1;
static void orient_apply(SDL_Window *w, SDL_Renderer *rr) {
    int sm = SV.intscale ? 1 : 0;
    if (sm != applied_scale) {                          /* smooth or sharp: each texture keeps its own filter, so make them again */
        tex_sharp = sm;
        for (int i = 0; i < PNJ_NTEX; i++) { if (TEX[i]) SDL_DestroyTexture(TEX[i]); TEX[i] = 0; TEXW[i] = 0; }
        if (tgt) SDL_DestroyTexture(tgt);
        tgt = NULL; tgt_w = tgt_h = 0; applied_scale = sm; applied_int = -1;
    }
    int wi = sm && !SV.orient;
    if (applied_orient != SV.orient || applied_int != wi) {
        if (SV.orient) SDL_SetRenderLogicalPresentation(rr, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        else SDL_SetRenderLogicalPresentation(rr, PNJ_W, PNJ_H, wi ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX);
        applied_int = wi;
    }
    if (applied_orient == SV.orient) return;
#ifndef __vita__
    SDL_SetWindowMinimumSize(w, SV.orient ? 360 : 240, SV.orient ? 240 : 360);
    if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) {
        int ww, wh; SDL_GetWindowSize(w, &ww, &wh);
        if ((SV.orient != 0) != (ww > wh)) SDL_SetWindowSize(w, wh, ww);
    }
#else
    (void)w;
#endif
    applied_orient = SV.orient;
}
static void draw_current(SDL_Renderer *rr) {
    if (hdrtest_mode) { draw_hdrtest(rr); return; }
    if (IJ.busy) { draw_import(rr); return; }
#ifndef __ANDROID__
    if (need_data) { draw_drop_hint(rr); return; }
#endif
    hdr_gain = ui_mode == UI_PLAY ? hdr_play : 1.0f;            /* the glow effect is for gameplay, not the menus */
    if (ui_mode == UI_PLAY) render(rr); else draw_menu(rr);
}
static void present_frame(SDL_Renderer *rr) {
    hdr_poll(rr);
    if (!SV.orient) { draw_current(rr); SDL_RenderPresent(rr); return; }
    int ow, oh; SDL_GetCurrentRenderOutputSize(rr, &ow, &oh);
    double sc = fmin(ow / (double)PNJ_H, oh / (double)PNJ_W);                     /* the turned field is PNJ_H wide and PNJ_W tall */
    if (SV.intscale && sc >= 1) sc = floor(sc);
    int tw = (int)(PNJ_W * sc) & ~1, th = (int)(PNJ_H * sc) & ~1; if (tw < 2) tw = 2; if (th < 2) th = 2;
    if (!tgt || tw != tgt_w || th != tgt_h || tgt_hdr != hdr_active) {
        if (tgt) SDL_DestroyTexture(tgt);
        tgt = SDL_CreateTexture(rr, hdr_active ? SDL_PIXELFORMAT_RGBA64_FLOAT : SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th); tgt_w = tw; tgt_h = th; tgt_hdr = hdr_active;
        if (tgt) SDL_SetTextureScaleMode(tgt, tex_sharp ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    }
    if (!tgt || !SDL_SetRenderTarget(rr, tgt)) { draw_current(rr); SDL_RenderPresent(rr); return; }
    SDL_SetRenderScale(rr, tw / (float)PNJ_W, th / (float)PNJ_H);
    draw_current(rr);
    SDL_SetRenderTarget(rr, NULL);
    SDL_SetRenderDrawColor(rr, 0, 0, 0, 255); SDL_RenderClear(rr);
    int x0 = (ow - tw) / 2, y0 = (oh - th) / 2; t_cx = x0 + tw / 2; t_cy = y0 + th / 2;
    SDL_FRect dst = { (float)x0, (float)y0, (float)tw, (float)th };
    SDL_RenderTextureRotated(rr, tgt, NULL, &dst, SV.orient == 2 ? 90.0 : -90.0, NULL, SDL_FLIP_NONE);
    SDL_RenderPresent(rr);
}
/* window position (window units) -> field position */
static void tate_map(SDL_Window *w, SDL_Renderer *rr, double x, double y, float *lx, float *ly) {
    int ww = 1, wh = 1, ow = 1, oh = 1; SDL_GetWindowSize(w, &ww, &wh); SDL_GetCurrentRenderOutputSize(rr, &ow, &oh);
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    double px = x * ow / ww, py = y * oh / wh, sx = tgt_w ? tgt_w / (double)PNJ_W : 1, sy = tgt_h ? tgt_h / (double)PNJ_H : 1;
    double cx = tgt_w ? t_cx : ow / 2.0, cy = tgt_w ? t_cy : oh / 2.0, X = px - cx, Y = py - cy;
    if (SV.orient == 2) { *lx = (float)(PNJ_W / 2.0 + Y / sx); *ly = (float)(PNJ_H / 2.0 - X / sy); }     /* top of the field faces right */
    else { *lx = (float)(PNJ_W / 2.0 - Y / sx); *ly = (float)(PNJ_H / 2.0 + X / sy); }                      /* top faces left */
}
static void mouse_field(SDL_Window *w, SDL_Renderer *rr, float x, float y, float *lx, float *ly) {
    if (SV.orient) tate_map(w, rr, x, y, lx, ly); else SDL_RenderCoordinatesFromWindow(rr, x, y, lx, ly);
}
static void finger_field(SDL_Window *w, SDL_Renderer *rr, float nx, float ny, float *lx, float *ly) {
    int ww, wh; SDL_GetWindowSize(w, &ww, &wh); mouse_field(w, rr, nx * ww, ny * wh, lx, ly);
}
#elif defined(PNJ_DC)
/* ---------- Dreamcast: one screen, the 320x480 field in the middle of 640x480 (dc_gfx.h) ---------- */
static int hdr_possible = 0;
static SDL_Renderer *make_renderer(SDL_Window *win, int want_hdr) { (void)win; (void)want_hdr; return pnj_gfx_open(); }
static void hdr_poll(SDL_Renderer *r) { (void)r; }
static SDL_Texture *tgt; static int tgt_w, tgt_h, t_cx, t_cy, applied_orient = -1, applied_scale = -1, applied_int = -1;
static void orient_apply(SDL_Window *w, SDL_Renderer *rr) { (void)w; (void)rr; dc_tate = SV.orient; }       /* tate: see dc_gfx.h */
static void draw_current(SDL_Renderer *rr) {
    if (need_data) { draw_drop_hint(rr); return; }
    hdr_gain = 1.0f;
    if (ui_mode == UI_PLAY) render(rr); else draw_menu(rr);
}
static void present_frame(SDL_Renderer *rr) {
    pnj_gfx_begin(rr);
    pnj_gfx_screen(rr);
    draw_current(rr);
    pnj_gfx_end(rr);
}
static void mouse_field(SDL_Window *w, SDL_Renderer *rr, float x, float y, float *lx, float *ly) { (void)w; (void)rr; *lx = x; *ly = y; }
static void finger_field(SDL_Window *w, SDL_Renderer *rr, float nx, float ny, float *lx, float *ly) { (void)w; (void)rr; *lx = nx * PNJ_W; *ly = ny * PNJ_H; }
#elif defined(PNJ_NDS)
/* ---------- Nintendo DS: the field at 0.8 across both screens, one screen drawn per frame (nds_gfx.h) ---------- */
static int hdr_possible = 0;
static SDL_Renderer *make_renderer(SDL_Window *win, int want_hdr) { (void)win; (void)want_hdr; return pnj_gfx_open(); }
static void hdr_poll(SDL_Renderer *r) { (void)r; }
static SDL_Texture *tgt; static int tgt_w, tgt_h, t_cx, t_cy, applied_orient = -1, applied_scale = -1, applied_int = -1;
static void orient_apply(SDL_Window *w, SDL_Renderer *rr) { (void)w; (void)rr; SV.orient = 0; }       /* no rotation on the DS */
static void draw_current(SDL_Renderer *rr) {
    if (need_data) { draw_drop_hint(rr); return; }
    hdr_gain = 1.0f;
    if (ui_mode == UI_PLAY) render(rr); else draw_menu(rr);
}
static void present_frame(SDL_Renderer *rr) {
    if (!need_data) {                                    /* the page for the stage on screen: the one being played, or the one picked in the menus */
        int scene = ui_mode == UI_PLAY || ui_mode == UI_OVER || (is_opt(ui_mode) && opt_back == UI_PLAY) || (ui_mode == UI_HELP && help_back == UI_PLAY);
        nds_page_set(scene ? G.stage : sel_stage);
    }
    pnj_gfx_begin(rr);
    draw_current(rr);
    pnj_gfx_end(rr);
}
static void mouse_field(SDL_Window *w, SDL_Renderer *rr, float x, float y, float *lx, float *ly) { (void)w; (void)rr; *lx = x; *ly = y; }
static void finger_field(SDL_Window *w, SDL_Renderer *rr, float nx, float ny, float *lx, float *ly) {      /* nx, ny: 0..1 over the touch screen, which is the lower half of the field */
    (void)w; (void)rr; *lx = nx * PNJ_W; *ly = PNJ_H / 2.0f + ny * (PNJ_H / 2.0f);
}
#else
/* ---------- 3DS: one renderer, two screens ---------- */
static int hdr_possible = 0;
static SDL_Renderer *make_renderer(SDL_Window *win, int want_hdr) { (void)win; (void)want_hdr; return pnj_gfx_open(); }
static void hdr_poll(SDL_Renderer *r) { (void)r; }
static SDL_Texture *tgt; static int tgt_w, tgt_h, t_cx, t_cy, applied_orient = -1, applied_scale = -1, applied_int = -1;
static void orient_apply(SDL_Window *w, SDL_Renderer *rr) { (void)w; (void)rr; SV.orient = 0; }       /* no rotation or scale options on the 3DS */
static void draw_current(SDL_Renderer *rr) {
    if (IJ.busy) { draw_import(rr); return; }
    if (need_data) { draw_drop_hint(rr); return; }
    hdr_gain = 1.0f;
    if (ui_mode == UI_PLAY) render(rr); else draw_menu(rr);
}
/* Menu mode: a menu, the pause overlay or game over is up. The menu goes on the touch screen only, at half size; the top screen keeps the scene. */
/* Which field rows the touch screen shows 1:1 for the current menu (-1: the whole menu at half size). Screens whose buttons fit in 240 rows get full size. */
static int n3ds_menu_win(void) {
    if (ui_mode == UI_MENU) return 150;
    if (ui_mode == UI_PLAY && paused) return 106;
    if (ui_mode == UI_OPTION) return 0;
    return -1;
}
static int n3ds_menu_mode(void) { return !IJ.busy && !need_data && (ui_mode != UI_PLAY || paused); }
static void draw_top_menu(SDL_Renderer *rr) {
    int scene = ui_mode == UI_PLAY || ui_mode == UI_OVER || (is_opt(ui_mode) && opt_back == UI_PLAY) || (ui_mode == UI_HELP && help_back == UI_PLAY);
    if (scene) { int kid = kb_id; pnj_top_pass = 1; render(rr); pnj_top_pass = 0; kb_id = kid; return; }   /* render() refreshes the focus against an empty hit list, which would wipe the touch screen's highlight */       /* the (paused) game, without the overlay */
    G.stage = sel_stage; G.L = ui_L; G.cam594 = (fx)(-ui_L * 4); G.peak = 0; G.p.power = 0;
    SDL_SetRenderDrawColor(rr, 0, 0, 0, 255); SDL_RenderClear(rr);
    draw_bg(rr);
    PNJ_DEPTH(rr, 0.5);
    if (ui_mode == UI_TITLE || ui_mode == UI_MENU) draw_logo(rr, 0, 1);
    PNJ_DEPTH(rr, 0);
}
/* texture viewer (L+R): top screen shows the whole sheet as the GPU holds it, touch screen a 1:1 close-up of one quarter (L+X changes it) */
static void draw_texview(SDL_Renderer *rr) {
    SDL_Texture *t = get_tex(rr, pnj_dbg_tex);
    for (int s = 0; s < 2; s++) {
        pnj_gfx_screen(rr, s, 0); rr->ox = 0; rr->oy = 0; rr->sc = 1.0f;
        SDL_SetRenderDrawColor(rr, 60, 0, 60, 255); SDL_FRect q = { 0, 0, 400, 240 }; SDL_RenderFillRect(rr, &q);
        if (!t) continue;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureAlphaMod(t, 255); SDL_SetTextureColorMod(t, 255, 255, 255);
        if (s == 0) { SDL_FRect src = { 0, 0, (float)t->w, (float)t->h }, dst = { 82, 2, 236, 236 }; SDL_RenderTexture(rr, t, &src, &dst); }
        else { int qd = pnj_dbg_sub & 3; SDL_FRect src = { (float)((qd & 1) * (t->w - 320)), (float)((qd >> 1) * (t->h - 240)), 320, 240 }, dst = { 0, 0, 320, 240 }; SDL_RenderTexture(rr, t, &src, &dst); }
    }
}
static void present_frame(SDL_Renderer *rr) {
    int stereo = pnj_gfx_stereo(rr);
    pnj_gfx_begin(rr);
    if (pnj_dbg_tex >= 0) { draw_texview(rr); pnj_gfx_end(rr); return; }
    int menu = n3ds_menu_mode();
    pnj_menu_win = menu ? n3ds_menu_win() : -1;
    for (int pass = 0; pass < 3; pass++) {                                    /* top left eye, top right eye (3D slider up only), touch screen */
        int s = pass == 2, eye = pass == 0 ? (stereo ? -1 : 0) : pass == 1 ? 1 : 0;
        if (pass == 1 && !stereo) continue;
        rr->eye = eye;
        pnj_gfx_screen(rr, s, menu);
        if (menu && s == 0) draw_top_menu(rr); else draw_current(rr);        /* playing: the same field twice, top rows 0-239, touch screen 240-479 */
    }
    rr->eye = 0;
    pnj_gfx_end(rr);
}
static void mouse_field(SDL_Window *w, SDL_Renderer *rr, float x, float y, float *lx, float *ly) { (void)w; (void)rr; *lx = x; *ly = y; }
static void finger_field(SDL_Window *w, SDL_Renderer *rr, float nx, float ny, float *lx, float *ly) {      /* nx, ny: 0..1 over the touch screen */
    (void)w; (void)rr; float px = nx * 320.0f, py = ny * 240.0f;
    if (n3ds_menu_mode() && pnj_menu_win >= 0) { *lx = px; *ly = py + (float)pnj_menu_win; }
    else if (n3ds_menu_mode()) { *lx = (px - 80.0f) * 2.0f; *ly = py * 2.0f; }           /* the menu sits at half size, centred on the touch screen */
    else { *lx = px; *ly = 240.0f + py; }                                           /* playing: the touch screen is the lower half of the field */
}
#endif

/* ---------- remappable keys and pad ---------- */
static int pad_l, pad_r, stick_l, stick_r;                  /* game controller: dpad and left stick as left/right */
static unsigned char pstate[SDL_GAMEPAD_BUTTON_COUNT]; static int sdir[2][4];   /* [left or right stick][up, down, left, right] */
static void recalc_pad(void) {
    pad_l = pad_r = 0;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) if (pstate[b]) { int a = act_of_pad(b); if (a == ACT_LEFT) pad_l = 1; else if (a == ACT_RIGHT) pad_r = 1; }
}
static void nav_act(int a) { if (a == ACT_LEFT) kb_nav(-1, 0); else if (a == ACT_RIGHT) kb_nav(1, 0); else if (a == ACT_UP) kb_nav(0, -1); else if (a == ACT_DOWN) kb_nav(0, 1); }
static void stick_dir(int which, int btn, int on) {         /* each stick counts as the D-pad, so it follows the same bindings (and, on the Vita, the same turn) */
    int i = btn - SDL_GAMEPAD_BUTTON_DPAD_UP; if (sdir[which][i] == on) return;
    sdir[which][i] = on;
    if (on && kb_active()) nav_act(act_of_pad(btn));
    stick_l = stick_r = 0;
    for (int s = 0; s < 2; s++) for (int d = 0; d < 4; d++) if (sdir[s][d]) { int a = act_of_pad(SDL_GAMEPAD_BUTTON_DPAD_UP + d); if (a == ACT_LEFT) stick_l = 1; else if (a == ACT_RIGHT) stick_r = 1; }
}
static void cap_key(SDL_Keycode k) {
    int a = cap / 3, sl = cap % 3;
    if (k == SDLK_ESCAPE) { cap = -1; return; }
    if (sl > 1) return;                                      /* waiting for a pad button */
    for (int x = 0; x < ACT_N; x++) for (int y = 0; y < 2; y++) if (SV.key[x][y] == (int)k) SV.key[x][y] = 0;
    SV.key[a][sl] = (int)k; cap = -1; save_write(); audio_play(A_DECIDE);
}
static void cap_pad(int b) {
    int a = cap / 3, sl = cap % 3;
    if (sl != 2 || b == SDL_GAMEPAD_BUTTON_GUIDE) return;
    for (int x = 0; x < ACT_N; x++) if (SV.pad[x] == b) SV.pad[x] = -1;
    SV.pad[a] = b; cap = -1; save_write(); audio_play(A_DECIDE);
}
static void data_loaded(void) {
    size_t n; unsigned char *b = read_asset("meta/atlas.txt", &n);
    if (b) { int k = atlas_parse((const char *)b, n); free(b); SDL_Log("atlas: %d sprites", k); }
    help_load();
}
#ifdef PNJ_INTERP
#include "interp.h"
static float interp_t = 1.0f;                            /* how far the clock is between the last two simulation steps, 0..1 */
#endif
#ifdef PNJ_DC
/* Loading screens. Everything that reads the disc or decodes (start-up, and the start of each level) happens behind one of these, so nothing
 * hitches while you play or move around the menus. A bar needs a known total, so the start-up screen counts its steps. */
static SDL_Renderer *dc_rr;
static int ld_done, ld_total, ld_note;
static void dc_loading(const char *what) {
    SDL_Renderer *r = dc_rr; if (!r) return;
    pnj_gfx_begin(r); pnj_gfx_screen(r); SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
    tf_text(r, "LOADING", 160, 150, 4, 255, 224, 122);
    if (ld_total) {
        SDL_FRect fr = { 40, 250, 240, 18 }, in = { 43, 253, (float)(234 * (ld_done > ld_total ? ld_total : ld_done) / ld_total), 12 };
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255); SDL_RenderRect(r, &fr);
        SDL_SetRenderDrawColor(r, 255, 224, 122, 255); if (in.w > 0) SDL_RenderFillRect(r, &in);
        char t[16]; snprintf(t, sizeof t, "%d%%", ld_total ? 100 * (ld_done > ld_total ? ld_total : ld_done) / ld_total : 0);
        tf_text(r, t, 160, 280, 2, 255, 255, 255);
    }
    if (what) tf_text(r, what, 160, 318, 2, 200, 200, 200);
    if (ld_note) {
        tf_text(r, "THE FIRST LOAD CAN TAKE", 160, 370, 2, 255, 255, 255);
        tf_text(r, "UP TO A MINUTE.", 160, 392, 2, 255, 255, 255);
        tf_text(r, "THE GAME LOADS ITS GRAPHICS", 160, 424, 1, 170, 170, 170);
        tf_text(r, "AND MUSIC NOW SO IT RUNS SMOOTHLY.", 160, 436, 1, 170, 170, 170);
    }
    pnj_gfx_end(r);
}
static void dc_boot_tick(const char *what) { ld_done++; dc_loading(what); }
#endif
static void new_game(uint32_t seed, int stage) {
#ifdef PNJ_DC
    if (dc_rr) {                                         /* this stage's two sheets and its music, now rather than mid-game */
        save_commit(); audio_music(""); dc_audio_hush(); ld_total = 0; ld_note = 0; dc_loading("GETTING THE STAGE READY");
        char tr[16]; snprintf(tr, sizeof tr, "bgm%d", stage + 2);
        get_tex(dc_rr, 5 + 2 * stage); get_tex(dc_rr, 6 + 2 * stage); audio_music_prefetch(tr);      /* normally already resident: every sheet is loaded at start-up */
        dc_audio_unhush();
    }
#endif
    cur_stage = stage; pnj_new(&G, seed, stage); IN.ramp = 0; touch_dir = 0; paused = 0;
#ifdef PNJ_INTERP
    interp_reset(&G);
#endif
}
#ifdef PNJ_N3DS
/* stereo depth by draw-list layer: platforms sit on the screen, pickups and enemies float in front, the player a little more, effects pop out.
 * Eaten items burst toward you, and on a power-up the enemies jump forward. */
static double draw_depth(const pnj_draw *d) {
    int sp = d->sprite;
    if (d->layer == 1) return 0.0;
    if (d->layer == 2) {
        if ((sp >= 169 && sp <= 173) || (sp >= 201 && sp <= 205)) return 0.9;        /* defeated enemy / pickup burst */
        double z = 0.35 + (G.p.power ? 0.35 : 0.0);
        return d->blend ? z + 0.35 : z;
    }
    if (d->layer == 3) return 0.55;
    if (d->layer >= 4) return 0.8;
    return 0.0;
}
#endif
static void render(SDL_Renderer *r) {
    nhits = 0;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
#ifdef PNJ_INTERP
    if (ui_mode == UI_PLAY && ID_ok) {                                  /* blend the last two simulation steps by how far the clock is between them */
        float t = paused ? 1.0f : interp_t, cs = (float)G.cam594; G.cam594 = (fx)interp_cam(t);
        draw_bg(r); G.cam594 = (fx)cs; interp_blend(&G, t);
    } else { draw_bg(r); pnj_build_draw(&G); }
#else
    draw_bg(r);
    pnj_build_draw(&G);
#endif
    for (int i = 0; i < G.ndraw; i++) {
        const pnj_draw *d = &G.draw[i];
#ifdef PNJ_N3DS
        PNJ_DEPTH(r, draw_depth(d));
#endif
        spr(r, d->sprite, d->col, d->row, d->vrow, d->x, d->y, d->rot, d->scale, d->a / 255.0, d->blend,
            d->r / 255.0, d->g / 255.0, d->b / 255.0);
    }
    PNJ_DEPTH(r, 0.3);
    draw_hud(r);
    PNJ_DEPTH(r, 0);
#ifdef PNJ_N3DS
    if (paused && ui_mode == UI_PLAY && !pnj_top_pass) draw_pause(r);
#else
    if (paused && ui_mode == UI_PLAY) { if (touch_ui_on()) SPR(r, 65, 160, 19); draw_pause(r); }
#endif   /* Options and Help opened from the pause menu draw over the bare scene */
    draw_hits(r);
}

/* returns 1 if the touch was consumed by a HUD control */
static int hud_touch(double lx, double ly) {
#ifndef PNJ_N3DS
    if (fabs(lx - 160) <= 32 && ly <= 52) { paused = !paused; return 1; }
#endif
    if (lx < 90 && ly < 32) { SV.analog = !SV.analog; save_apply(); save_write(); IN.active = 0; touch_dir = 0; return 1; }
    if (lx < 150 && ly >= 32 && ly < 72) { SV.snappy = !SV.snappy; save_apply(); save_write(); return 1; }
    return 0;
}
static void touch_begin(double lx, double ly) {
    tap_x = lx; tap_y = ly; kb_on = 0;
    if (ui_mode != UI_PLAY || paused) {                       /* menus and the pause overlay */
        if (ui_mode == UI_PLAY && fabs(lx - 160) <= 32 && ly <= 52) { paused = 0; return; }
        ui_touch(lx, ly); return;
    }
    if (hud_touch(lx, ly)) return;
    if (ctrl_analog) pnj_touch_down(&IN, lx, ly); else touch_dir = lx < 160 ? -1 : 1;
}
static void touch_move(double lx, double ly) {
    if (ui_mode != UI_PLAY) { ui_drag(lx); return; }
    if (paused) { ui_drag(lx); return; }
    if (ctrl_analog) pnj_touch_move(&IN, lx, ly); else if (touch_dir) touch_dir = lx < 160 ? -1 : 1;
}
static void touch_end(void) { pnj_touch_up(&IN); touch_dir = 0; drag_id = H_NONE; save_write(); }

/* Several fingers at once (desktop touchscreens, tablets). One finger drives menus and the analog pad; with the digital pad in play
 * every finger counts and the most recent one decides left or right. Fingers on a trackpad (not a direct touch device) are ignored. */
#define MAXF 10
static struct { SDL_FingerID id; double x; int on; } fing[MAXF];
static int fprim = -1;
static int digital_play(void) { return ui_mode == UI_PLAY && !paused && !ctrl_analog; }
static int fslot(SDL_FingerID id) { for (int i = 0; i < MAXF; i++) if (fing[i].on && fing[i].id == id) return i; return -1; }
static void finger_event(Uint32 type, SDL_FingerID id, double lx, double ly) {
    int i = fslot(id);
    if (type == SDL_EVENT_FINGER_DOWN) {
        if (i >= 0) { fing[i].on = 0; if (i == fprim) { fprim = -1; touch_end(); } }      /* a down for a finger we think is still down: its up was lost, start over */
        for (i = 0; i < MAXF && fing[i].on; i++) ;
        if (i == MAXF) return;
        fing[i].on = 1; fing[i].id = id; fing[i].x = lx;
        if (fprim < 0) { fprim = i; touch_begin(lx, ly); }
        else if (digital_play()) touch_dir = lx < 160 ? -1 : 1;
    } else if (i < 0) {
        return;
    } else if (type == SDL_EVENT_FINGER_MOTION) {
        fing[i].x = lx;
        if (i == fprim) touch_move(lx, ly); else if (digital_play()) touch_dir = lx < 160 ? -1 : 1;
    } else {                                                  /* finger up */
        fing[i].on = 0;
        int rest = -1; for (int k = 0; k < MAXF; k++) if (fing[k].on) rest = k;
        if (rest >= 0 && digital_play()) { if (i == fprim) fprim = rest; touch_dir = fing[rest].x < 160 ? -1 : 1; return; }
        if (i == fprim) { fprim = -1; touch_end(); for (int k = 0; k < MAXF; k++) fing[k].on = 0; }
    }
}

static double current_tilt(void) {
    int sl = IN.left, sr = IN.right;                       /* keyboard state; touch side and controller are merged in for this step only */
    IN.left = sl || (!ctrl_analog && touch_dir < 0) || pad_l || stick_l;
    IN.right = sr || (!ctrl_analog && touch_dir > 0) || pad_r || stick_r;
    double t = pnj_input_tilt(&IN);
    IN.left = sl; IN.right = sr;
    return t;
}

#if defined(PNJ_N3DS) || defined(PNJ_SHIM)
static int selftest(int steps, uint32_t seed, const char *bmp, int stage, int level) { (void)steps; (void)seed; (void)bmp; (void)stage; (void)level; return 2; }
#else
static int selftest(int steps, uint32_t seed, const char *bmp, int stage, int level) {
    SDL_Window *w = SDL_CreateWindow("pnj", PNJ_W, PNJ_H, SDL_WINDOW_HIDDEN);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, "software") : NULL;
    if (!r) { fprintf(stderr, "no renderer: %s\n", SDL_GetError()); return 2; }
    save_load(); SV.top[0][0] = 12345; SV.top[0][1] = 9000; SV.top[0][2] = 500; SV.top[1][0] = 777;
    new_game(seed, stage); G.start_level = level;
    FILE *tf = getenv("PNJ_TILTS") ? fopen(getenv("PNJ_TILTS"), "r") : NULL;   /* replay a recorded bot run: header "seed stage n level" then tilts */
    if (tf) { unsigned a; int b, c, d; if (fscanf(tf, "%u %d %d %d", &a, &b, &c, &d) != 4) { fclose(tf); tf = NULL; } else { new_game(a, b); G.start_level = d; } }
    for (int i = 0; i < steps; i++) {
        double t = 0.5 * sin(i * 0.07 + seed);
        if (tf) { if (fscanf(tf, "%lf", &t) != 1) break; }
        pnj_step(&G, (fx)t);
    }
    if (tf) fclose(tf);
    if (getenv("PNJ_PAUSE")) { paused = 1; render(r); } else if (getenv("PNJ_MENU")) { sel_stage = stage; int m = atoi(getenv("PNJ_MENU")); if (m == UI_OVER) { over_score = 4321; over_new = 1; over_nach = 4; over_ach[0] = 3; over_ach[1] = 12; over_ach[2] = 80; over_ach[3] = 81; } ui_enter(m); ui_L = steps; draw_menu(r); } else
    render(r);
    SDL_Surface *s0 = SDL_RenderReadPixels(r, NULL);
    SDL_Surface *s = s0 ? SDL_ConvertSurface(s0, SDL_PIXELFORMAT_ARGB8888) : NULL;
    SDL_DestroySurface(s0);
    if (bmp && s) SDL_SaveBMP(s, bmp);
    int loaded = 0; for (int i = 0; i < PNJ_NTEX; i++) if (TEX[i]) loaded++;
    printf("selftest steps=%d x=%.3f y=%.3f cam=%.3f height=%d draw=%d nplat=%d textures=%d\n", steps, (double)G.p.x, (double)G.p.y,
           (double)G.cam594, pnj_height(&G), G.ndraw, G.nplat, loaded);
    SDL_DestroySurface(s); SDL_DestroyRenderer(r); SDL_DestroyWindow(w);
    return 0;
}
#endif

#ifdef PNJ_N3DS
static int no_audio;                                  /* set at start-up when the 3DS has no usable DSP route (see the sound block in main) */
#else
#define no_audio 0
#endif
#if !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
#include "dtx.h"
/* --dcpack in.pack [--out out.pack]: copies a pack and adds the Dreamcast textures (dc/<sheet>.dtx, see dtx.h). Everything else is copied as it is. */
static int dcpack(const char *in, const char *outp) {
    if (!pack_open(in)) { fprintf(stderr, "cannot read the pack %s\n", in); return 1; }
    int cap = PKN + PNJ_NTEX + 1, nf = 0;
    char (*nm)[56] = (char (*)[56])calloc((size_t)cap, 56); unsigned char **dat = (unsigned char **)calloc((size_t)cap, sizeof *dat); size_t *sz = (size_t *)calloc((size_t)cap, sizeof *sz);
    if (!nm || !dat || !sz) { fprintf(stderr, "out of memory\n"); return 1; }
    for (int i = 0; i < PKN; i++) {
        if (!strncmp(PK[i].name, "dc/", 3)) continue;                 /* rebuilt below */
        size_t n; unsigned char *b = pack_read(PK[i].name, &n);
        if (!b) { fprintf(stderr, "cannot read %s from the pack\n", PK[i].name); return 1; }
        snprintf(nm[nf], 56, "%s", PK[i].name); dat[nf] = b; sz[nf] = n; nf++;
    }
    int made = 0;
    for (int t = 0; t < PNJ_NTEX; t++) {
        char pn[64]; snprintf(pn, sizeof pn, "tex/%s.png", PNJ_TEXTURES[t]);
        size_t n; unsigned char *b = pack_read(pn, &n); if (!b) continue;
        int w = 0, h = 0, c = 0; unsigned char *px = stbi_load_from_memory(b, (int)n, &w, &h, &c, 4); free(b);
        if (!px) { fprintf(stderr, "cannot decode %s\n", pn); continue; }
        int sh = w >= 512 ? 2 : 1, dw = w / sh, dh = h / sh;
        if ((dw & (dw - 1)) || (dh & (dh - 1)) || dw < 8 || dh < 8 || dw > 1024 || dh > 1024) { fprintf(stderr, "%s: size %dx%d does not suit the console, skipped\n", pn, dw, dh); stbi_image_free(px); continue; }
        uint16_t *lin = (uint16_t *)malloc((size_t)dw * dh * 2); if (!lin) { fprintf(stderr, "out of memory\n"); return 1; }
        pnj_pack4444(px, w, h, sh, lin); stbi_image_free(px);
        size_t on = 8 + (size_t)dw * dh * 2; int vq = (dw == dh);
        if (vq) on = 8 + 2048 + (size_t)(dw / 2) * (dh / 2);
        unsigned char *o = (unsigned char *)malloc(on); if (!o) { fprintf(stderr, "out of memory\n"); return 1; }
        memcpy(o, vq ? "DTX2" : "DTX1", 4); o[4] = (unsigned char)dw; o[5] = (unsigned char)(dw >> 8); o[6] = (unsigned char)dh; o[7] = (unsigned char)(dh >> 8);
        if (vq) { if (!pnj_vq4444(lin, dw, dh, o + 8)) { fprintf(stderr, "%s: could not compress\n", pn); free(o); free(lin); continue; } }
        else memcpy(o + 8, lin, (size_t)dw * dh * 2);
        free(lin);
        printf("\r%-16s %dx%d %s %6zu bytes\n", PNJ_TEXTURES[t], dw, dh, vq ? "VQ  " : "raw ", on); fflush(stdout);
        snprintf(nm[nf], 56, "dc/%s.dtx", PNJ_TEXTURES[t]); dat[nf] = o; sz[nf] = on; nf++; made++;
    }
    if (!made) { fprintf(stderr, "no sprite sheets found in the pack\n"); return 1; }
    FILE *f = fopen(outp, "wb"); if (!f) { fprintf(stderr, "cannot write %s\n", outp); return 1; }
    unsigned char hd[12]; memcpy(hd, "PNJPACK1", 8); hd[8] = (unsigned char)nf; hd[9] = (unsigned char)(nf >> 8); hd[10] = hd[11] = 0;
    fwrite(hd, 1, 12, f);
    int *ord = (int *)malloc((size_t)nf * sizeof(int)); if (!ord) { fprintf(stderr, "out of memory\n"); return 1; }
    { int k = 0; for (int pass = 0; pass < 3; pass++) for (int i = 0; i < nf; i++) {          /* the console's boot sweep reads dc/ first; the PNG sheets are never read there */
        int key = !strncmp(nm[i], "dc/", 3) ? 0 : !strncmp(nm[i], "tex/", 4) ? 2 : 1; if (key == pass) ord[k++] = i; } }
    size_t off = 12; for (int i = 0; i < nf; i++) off += 2 + strlen(nm[i]) + 8;
    for (int oi = 0; oi < nf; oi++) {
        int i = ord[oi]; size_t l = strlen(nm[i]); unsigned char e[2]; e[0] = (unsigned char)l; e[1] = (unsigned char)(l >> 8); fwrite(e, 1, 2, f); fwrite(nm[i], 1, l, f);
        unsigned char v[8]; v[0] = (unsigned char)off; v[1] = (unsigned char)(off >> 8); v[2] = (unsigned char)(off >> 16); v[3] = (unsigned char)(off >> 24);
        v[4] = (unsigned char)sz[i]; v[5] = (unsigned char)(sz[i] >> 8); v[6] = (unsigned char)(sz[i] >> 16); v[7] = (unsigned char)(sz[i] >> 24); fwrite(v, 1, 8, f);
        off += sz[i];
    }
    for (int oi = 0; oi < nf; oi++) { int i = ord[oi]; fwrite(dat[i], 1, sz[i], f); free(dat[i]); }
    free(ord); fclose(f);
    printf("%s: %d files, %d Dreamcast textures added\n", outp, nf, made);
    return 0;
}
#endif

int main(int argc, char *argv[]) {
    const char *dcpack_arg = NULL;
    int selftest_steps = 0; uint32_t seed = 0; const char *bmp = NULL; int st_arg = 0, lv_arg = 0; const char *pack_arg = NULL, *import_arg = NULL, *import_out = NULL; int packinfo = 0, audiotest = 0, start_fs = 0, cli_orient = -1, windowed = 0, cli_hdr = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftest_steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc) st_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) lv_arg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bmp") && i + 1 < argc) bmp = argv[++i];
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_arg = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("Pac'N-Jump native %s\n", PNJ_VERSION); return 0; }
        else if (!strcmp(argv[i], "--packinfo")) packinfo = 1;
        else if (!strcmp(argv[i], "--import") && i + 1 < argc) import_arg = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) import_out = argv[++i];
        else if (!strcmp(argv[i], "--audiotest")) audiotest = 1;
        else if (!strcmp(argv[i], "--dcpack") && i + 1 < argc) dcpack_arg = argv[++i];
        else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "--tate")) start_fs = 1;
        else if (!strcmp(argv[i], "--windowed")) windowed = 1;
        else if (!strcmp(argv[i], "--hdr")) cli_hdr = 1;
        else if (!strcmp(argv[i], "--no-hdr")) cli_hdr = 0;
        else if (!strcmp(argv[i], "--hdrtest")) { hdrtest_mode = 1; cli_hdr = 1; }
        else if (!strcmp(argv[i], "--orient") && i + 1 < argc) { const char *o = argv[++i]; cli_orient = !strcmp(o, "left") ? 1 : !strcmp(o, "right") ? 2 : 0; }
    }
#ifdef _WIN32
    { extern __declspec(dllimport) int __stdcall AttachConsole(unsigned long);   /* GUI exe: show --import/--packinfo output in the terminal it was started from */
      if (AttachConsole((unsigned long)-1)) { freopen("CONOUT$", "w", stdout); freopen("CONOUT$", "w", stderr); } }
#endif
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#ifdef __vita__
    SDL_SetHint(SDL_HINT_VITA_ENABLE_BACK_TOUCH, "0");     /* the rear pad would send stray taps */
#endif
#if defined(PNJ_N3DS) || defined(PNJ_SHIM)
    if (!SDL_Init(SDL_INIT_EVENTS)) {                       /* no SDL video on the 3DS: citro2d owns the screens (n3ds_gfx.h) */
#else
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
#endif
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#ifdef PNJ_N3DS
    /* Sound. A dumped sdmc:/3ds/dspfirm.cdc is used when it is there (plain ndsp, SDL's audio backend initialises it). Without one, OpenDSP is only tried if
       sdmc:/3ds/pacnjump/opendsp exists: on a New 3DS XL it faulted reading the DSP memory (opendspGetCapabilities), so it is opt-in until that is understood.
       With neither, the game runs without sound. sdmc:/3ds/pacnjump/log.txt records which route was taken, written before the risky call. */
    {
        FILE *f = fopen("sdmc:/3ds/dspfirm.cdc", "rb"); int have_fw = f != NULL; if (f) fclose(f);
        f = fopen("sdmc:/3ds/pacnjump/opendsp", "rb"); int try_od = f != NULL; if (f) fclose(f);
        const char *route = have_fw ? "dspfirm.cdc" : try_od ? "opendsp" : "no sound (no dspfirm.cdc; make an empty file named opendsp in this folder to try OpenDSP)";
        f = fopen("sdmc:/3ds/pacnjump/log.txt", "w"); if (f) { fprintf(f, "sound route: %s\n", route); fflush(f); fclose(f); }
        if (!have_fw && !try_od) no_audio = 1;
        else if (!have_fw) { Result orc = opendspInit(); SDL_Log("opendsp: init %08lX", (unsigned long)orc); if (R_FAILED(orc)) no_audio = 1; }
    }
#endif
    no_bundle = getenv("PNJ_NO_BUNDLE") != NULL;
    if (pack_find(pack_arg)) { pack_load_maps(); SDL_Log("game data pack: %s (%d files, %d maps)", pack_path, PKN, PK_NMAPS); }
    data_loaded();
    if (import_arg) {
        int ok = 0;
        if (import_start(import_arg, import_out, 0)) {
            int shown = -1;
            while (!IJ.done) { int pc = imp_pm / 10; if (pc != shown) { shown = pc; printf("\r%3d%%  %-22s", pc, imp_stage_name(imp_stage)); fflush(stdout); } SDL_Delay(20); }
            printf("\r%-34s\r", ""); ok = import_finish();
        }
        printf("%s\n", imp_msg); SDL_Quit(); return ok ? 0 : 1; }
#if !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
    if (dcpack_arg) { int rc = dcpack(dcpack_arg, import_out ? import_out : "pnj_dc.pack"); SDL_Quit(); return rc; }
#else
    (void)dcpack_arg;
#endif
    if (packinfo) { printf("pack=%s files=%d maps=%d\n", PKN ? pack_path : "(none)", PKN, PK_NMAPS); SDL_Quit(); return PKN ? 0 : 1; }
#ifndef PNJ_NDS
    if (audiotest) {
        audio_init();
        int ok = 0, n = 0; for (int i = 1; i < 32; i++) if (SFXB[i].buf) ok++; n = ok;
        audio_music("bgm3"); audio_play(PNJ_SFX_JUMP); audio_play(PNJ_SFX_EAT3); SDL_Delay(700);
        printf("audiotest dev=%u sfx loaded=%d music=%s vb=%p\n", (unsigned)aud_dev, n, mus_name, (void *)mus_vb);
        audio_music("wakawaka"); SDL_Delay(300);
        printf("audiotest jingle: now=%s pcm_frames=%u (stage music keeps playing if the jingle is not in the pack)\n", mus_name, (unsigned)mus_frames);
        SDL_Quit(); return 0;
    }
#else
    (void)audiotest;
#endif
#ifndef PNJ_DC
    if (!selftest_steps && !no_audio) audio_init();      /* the Dreamcast loads its audio behind the loading screen below */
#endif
    if (selftest_steps) { int rc = selftest(selftest_steps, seed ? seed : 1, bmp, st_arg, lv_arg); SDL_Quit(); return rc; }

    if (getenv("SteamDeck") || getenv("SteamGamepadUI")) start_fs = 1;        /* Steam sets these when it launches a game on a Steam Deck */
    if (windowed) start_fs = 0;
#if defined(PNJ_N3DS) || defined(PNJ_SHIM)
    SDL_Window *win = SDL_CreateWindow("Pac'N-Jump", 320, 480, 0);
#elif defined(__vita__)
    SDL_Window *win = SDL_CreateWindow("Pac'N-Jump", 960, 544, SDL_WINDOW_FULLSCREEN);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
#else
    int win_h = 720, win_w = 480;                           /* portrait 2:3, as tall as is comfortable on this display */
    { SDL_Rect ub; if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &ub) && ub.w > 0 && ub.h > 0) {
        win_h = ub.h * 88 / 100; if (win_h > 960) win_h = 960; win_w = win_h * 2 / 3;
        if (win_w > ub.w * 9 / 10) { win_w = ub.w * 9 / 10; win_h = win_w * 3 / 2; }
        if (win_h < 480) { win_h = 480; win_w = 320; } } }
    SDL_Window *win = SDL_CreateWindow("Pac'N-Jump  " PNJ_VERSION, win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (start_fs ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    if (!start_fs) SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowMinimumSize(win, 240, 360);
#endif
    save_load(); if (cli_orient >= 0) SV.orient = cli_orient; if (cli_hdr >= 0) SV.hdr = cli_hdr;
    int applied_hdr = SV.hdr;
    SDL_Renderer *r = make_renderer(win, applied_hdr);
    if (!r) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderLogicalPresentation(r, PNJ_W, PNJ_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#if !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    { int ng = 0; SDL_JoystickID *ids = SDL_GetGamepads(&ng); for (int i = 0; i < ng; i++) SDL_OpenGamepad(ids[i]); SDL_free(ids); }
#endif

    srand((unsigned)SDL_GetTicks()); ui_enter(UI_TITLE); orient_apply(win, r);
    need_data = !data_present();
#ifdef PNJ_DC
    dc_rr = r;
    if (!need_data) {                                    /* sound, music and the sheets every menu uses: load them now behind a loading screen, so no menu hitches later */
        ld_done = 0; ld_total = 25 + 2 + PNJ_NTEX; ld_note = 1; boot_cb = dc_boot_tick; dc_loading("STARTING");
        audio_init(); boot_cb = NULL; dc_audio_hush();   /* nothing refills the sound stream while this screen waits */
        for (int i = 0; i < PNJ_NTEX; i++) { dc_loading("GRAPHICS"); get_tex(r, i); ld_done++; }   /* all of them: with the VQ sheets they fit in video memory together, so no menu or stage change reads the disc */
        dc_loading("DONE"); ld_note = 0; dc_audio_unhush();
    } else audio_init();
#endif
#if !defined(__ANDROID__) && !defined(__vita__) && !defined(PNJ_N3DS) && !defined(PNJ_SHIM)
    if (need_data) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Game data needed", DATA_HELP, win);
#endif
    if (seed) start_stage(0);
    const double step_ms = 1000.0 / PNJ_FPS;
    double acc = 0; Uint64 last = SDL_GetPerformanceCounter(); int run = 1;
    while (run) {
        SDL_Event e;
#ifdef PNJ_N3DS
        pnj_n3ds_pump();                                    /* buttons, circle pad and touch screen -> SDL events */
#endif
#ifdef PNJ_DC
        pnj_dc_pump();                                      /* controller -> SDL events, and the audio stream is topped up */
#endif
#ifdef PNJ_NDS
        pnj_nds_pump();                                     /* buttons and the touch screen -> SDL events */
#endif
        while (SDL_PollEvent(&e)) {
            if (IJ.busy && e.type != SDL_EVENT_QUIT && e.type != SDL_EVENT_DROP_FILE && !(e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) && e.type != SDL_EVENT_WILL_ENTER_BACKGROUND && e.type != SDL_EVENT_DID_ENTER_FOREGROUND) continue;   /* importing: the window only draws the loader */
            switch (e.type) {
            case SDL_EVENT_QUIT: run = 0; break;
            case SDL_EVENT_DROP_FILE: {
                const char *f = e.drop.data;
                int rm = f && strstr(f, "/import.apk") != NULL;    /* Android: Java's temporary copy of the picked file, deleted once imported */
                if (f && ends_apk(f) && !IJ.busy) {
                    if (!import_start(f, NULL, rm)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", imp_msg, win); if (rm) remove(f); }
                    else { cap = -1; IN.left = IN.right = 0; }
                } else if (rm) remove(f);
                break; }
            case SDL_EVENT_WILL_ENTER_BACKGROUND: paused = 1; audio_pause(1); break;
            case SDL_EVENT_DID_ENTER_FOREGROUND: audio_pause(0); break;
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
                int dn = e.type == SDL_EVENT_KEY_DOWN;
                SDL_Keycode k = e.key.key;
                if (k == SDLK_KP_ENTER) k = SDLK_RETURN;
                if (cap >= 0) { if (dn && !e.key.repeat) cap_key(k); break; }                    /* controls screen: the next key becomes the binding */
                if (dn && k != SDLK_AC_BACK && k != SDLK_VOLUMEUP && k != SDLK_VOLUMEDOWN && k != SDLK_MUTE && k != SDLK_POWER) in_dev = IND_KEY;
                if (dn && !e.key.repeat && (ui_mode == UI_TITLE || ui_mode == UI_OVER) && !(e.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_CTRL)) &&
                    k != SDLK_F11 && k != SDLK_AC_BACK && k != SDLK_VOLUMEUP && k != SDLK_VOLUMEDOWN &&
                    !(k >= SDLK_LCTRL && k <= SDLK_RGUI) && k != SDLK_CAPSLOCK && k != SDLK_NUMLOCKCLEAR && k != SDLK_SCROLLLOCK && !(ui_mode == UI_TITLE && act_of_key(k) == ACT_BACK)) {
                    ui_touch(PNJ_W / 2.0, PNJ_H / 2.0); break; }                                    /* title and game over: any key continues */
                if (dn && (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {   /* F11 / Alt+Enter */
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                    SDL_ShowCursor(); break; }
                int act = act_of_key(k);
                if (act == ACT_LEFT || act == ACT_RIGHT) {
                    if (dn && kb_active()) nav_act(act);                                          /* menus step on every press, repeats included */
                    else { IN.left = act_held(ACT_LEFT); IN.right = act_held(ACT_RIGHT); }        /* playing, or a release after a menu opened: never leave steering stuck */
                }
                else if (act == ACT_UP || act == ACT_DOWN) { if (dn && kb_active()) nav_act(act); }
                else if (act == ACT_BACK) { if (dn) { if (ui_mode == UI_TITLE) run = 0; else kb_back(); } }
                else if (act == ACT_OK) { if (dn && !e.key.repeat && !(e.key.mod & SDL_KMOD_ALT) && kb_active()) kb_activate(); }
                else if (act == ACT_PAUSE) { if (dn) paused = !paused; }
                else if (k == SDLK_R && dn && ui_mode == UI_PLAY) new_game((uint32_t)SDL_GetTicks() + 1, cur_stage);
                else if (k >= SDLK_1 && k <= SDLK_8 && dn && ui_mode == UI_STAGES) { sel_stage = (int)(k - SDLK_1); pnj_new(&G, 1, sel_stage); }
                break; }
            case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WINDOW_MINIMIZED:
                if (ui_mode == UI_PLAY) paused = 1; audio_pause(1); break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: case SDL_EVENT_WINDOW_RESTORED:
                audio_pause(0); last = SDL_GetPerformanceCounter(); acc = 0; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                int dn = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, b = e.gbutton.button;
                if (cap >= 0) { if (dn) cap_pad(b); break; }
                if (dn) in_dev = IND_PAD;
                if (dn && (ui_mode == UI_TITLE || ui_mode == UI_OVER)) { ui_touch(PNJ_W / 2.0, PNJ_H / 2.0); break; }   /* any button continues */
                if (b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT) { pstate[b] = (unsigned char)dn; recalc_pad(); }
                int act = act_of_pad(b);
                if (act >= ACT_LEFT && act <= ACT_DOWN) { if (dn && kb_active()) nav_act(act); }
                else if (act == ACT_OK) { if (dn && kb_active()) kb_activate(); }
                else if (act == ACT_BACK) { if (dn && ui_mode != UI_TITLE) kb_back(); }
                else if (act == ACT_PAUSE) { if (dn) { if (ui_mode == UI_PLAY) paused = !paused; else kb_activate(); } }
                break; }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (cap >= 0) break;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) { stick_dir(0, SDL_GAMEPAD_BUTTON_DPAD_LEFT, e.gaxis.value < -12000); stick_dir(0, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, e.gaxis.value > 12000); }
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) { stick_dir(0, SDL_GAMEPAD_BUTTON_DPAD_UP, e.gaxis.value < -12000); stick_dir(0, SDL_GAMEPAD_BUTTON_DPAD_DOWN, e.gaxis.value > 12000); }
#ifdef __vita__
                /* the right stick steers too: held turned, it is the one under the other thumb */
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX) { stick_dir(1, SDL_GAMEPAD_BUTTON_DPAD_LEFT, e.gaxis.value < -12000); stick_dir(1, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, e.gaxis.value > 12000); }
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) { stick_dir(1, SDL_GAMEPAD_BUTTON_DPAD_UP, e.gaxis.value < -12000); stick_dir(1, SDL_GAMEPAD_BUTTON_DPAD_DOWN, e.gaxis.value > 12000); }
#endif
                break;
            case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: {
#ifndef PNJ_N3DS       /* the 3DS has no SDL video device, and SDL's touch lookup dereferences it: the touch screen is the one and only device there */
                SDL_TouchDeviceType tt = SDL_GetTouchDeviceType(e.tfinger.touchID);
                if (tt == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE || tt == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE) break;   /* trackpad, not the screen */
#endif
                if (e.type == SDL_EVENT_FINGER_DOWN) in_dev = IND_TOUCH;
                float lx, ly; finger_field(win, r, e.tfinger.x, e.tfinger.y, &lx, &ly);
                SDL_HideCursor();
                finger_event(e.type, e.tfinger.fingerID, lx, ly);
                break; }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_MOTION: {
                Uint32 which = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which;
                if (which == SDL_TOUCH_MOUSEID) break;     /* touch already handled above */
                SDL_ShowCursor();
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) in_dev = IND_MOUSE;
                float lx, ly;
                if (e.type == SDL_EVENT_MOUSE_MOTION) { mouse_field(win, r, e.motion.x, e.motion.y, &lx, &ly); touch_move(lx, ly); }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) { mouse_field(win, r, e.button.x, e.button.y, &lx, &ly); touch_begin(lx, ly); }
                else touch_end();
                break; }
            }
        }
#ifdef PNJ_N3DS
        if (!run) break;                                    /* the system asked us to close: do not draw another frame, the GPU service may already be gone and the draw would never return */
#endif
        fs_state = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        if (fs_req) { fs_req = 0; SDL_SetWindowFullscreen(win, !fs_state); }
        if (SV.hdr != applied_hdr) {                        /* HDR switched in Options: the output colorspace is fixed per renderer, so make a new one */
            for (int i = 0; i < PNJ_NTEX; i++) { TEX[i] = 0; TEXW[i] = 0; }
            tgt = NULL; tgt_w = tgt_h = 0; SDL_DestroyRenderer(r);
            applied_hdr = SV.hdr; r = make_renderer(win, applied_hdr);
            if (!r) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
            applied_orient = applied_scale = applied_int = -1; last = SDL_GetPerformanceCounter(); acc = 0;
        }
        if (IJ.busy) {
            if (IJ.done) {
                int ok = import_finish(); if (ok) reset_assets();
                SDL_ShowSimpleMessageBox(ok ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, ok ? "Game data ready" : "Import failed", imp_msg, win);
                last = SDL_GetPerformanceCounter(); acc = 0; continue;
            }
            orient_apply(win, r); present_frame(r); SDL_Delay(16);
            last = SDL_GetPerformanceCounter(); acc = 0; continue;
        }
        if (need_data) {                                 /* nothing to play yet: wait for an APK */
#if defined(__ANDROID__) || defined(__vita__) || defined(PNJ_N3DS)
            static int tick; if (++tick % 60 == 0) scan_for_apk();
#endif
            orient_apply(win, r); present_frame(r); SDL_Delay(16);
            last = SDL_GetPerformanceCounter(); acc = 0; continue;
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (paused) acc = 0;
        int n = 0;
        while (acc >= step_ms && n < 5) {
            ui_L++;
            if (ui_mode != UI_PLAY) { acc -= step_ms; n++; continue; }
            pnj_step(&G, (fx)current_tilt());
#ifdef PNJ_INTERP
            interp_snap(&G);
#endif
            for (int i = 0; i < G.nev; i++) audio_play(G.ev[i] == PNJ_SFX_WARN ? A_WARN : G.ev[i]);   /* core sfx ids 1..18 match the mixer slots; the boss warning has its own */
            G.nev = 0;
            acc -= step_ms; n++;
            if (G.over) { run_finished(); break; }
        }
        if (n == 5) acc = 0;
#ifdef PNJ_DC
        if (ui_mode != UI_PLAY || paused) save_tick();          /* a pending memory card write goes out once the menus have been left alone for a moment */
#endif
#ifdef PNJ_INTERP
        interp_t = (float)(acc / step_ms); if (interp_t > 1.0f) interp_t = 1.0f;
#endif
        { char tr[16]; snprintf(tr, sizeof tr, "bgm%d", cur_stage + 2);
          audio_music(ui_mode == UI_OVER ? "" : ui_mode != UI_PLAY ? "bgm1" : (paused || G.p.dead) ? "" : G.p.power ? "wakawaka" : tr); }
        orient_apply(win, r);
        present_frame(r);
    }
#ifdef PNJ_N3DS
    pnj_exit_log("loop ended"); pnj_exit_watchdog();
#endif
    if (IJ.busy) { SDL_WaitThread(IJ.th, NULL); IJ.th = NULL; IJ.busy = 0; if (IJ.rm) remove(IJ.apk); }   /* closed mid-import: let the pack finish writing */
#ifdef PNJ_N3DS
    pnj_exit_log("import done, closing graphics");
#endif
    SDL_DestroyRenderer(r); SDL_DestroyWindow(win);
#ifdef PNJ_N3DS
    pnj_exit_log("graphics closed, SDL_Quit");
#endif
    SDL_Quit();
#ifdef PNJ_N3DS
    pnj_exit_log("clean");
#endif
    return 0;
}

#ifdef __vita__
/* VitaSDK reads these two at start-up: the C heap (the APK import and the sprite sheets need room) and the default thread stack */
unsigned int _newlib_heap_size_user = 200 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 1024 * 1024;
#endif

/* Menus for the SDL shell: title, main menu, stage select, records, options, pause, game over, plus saved settings/records.
 * Included by main.c after the HUD code (single translation unit). Uses the game's dotted font from the sysparts sheet. */
static void render(SDL_Renderer *r);
static void new_game(uint32_t seed, int stage);

static int stage_ready(int s) { (void)s; return 1; }   /* all 8 stages are in the core */
static int cur_stage = 0;

/* ---------- saved settings and records ---------- */
enum { ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN, ACT_OK, ACT_BACK, ACT_PAUSE, ACT_N };      /* remappable actions: steer, menu directions, select, back, pause */
/* New fields go at the end: older save files are shorter and keep working (the missing fields take their defaults). */
typedef struct { uint32_t magic; int sfx_on, mus_on; float sfx_vol, mus_vol; int analog, snappy; int top[8][5]; int total;
                 int orient;                 /* 0 standard, 1 tate left (top of the game faces left), 2 tate right */
                 int key[ACT_N][2];          /* SDL keycodes, 0 = none */
                 int pad[ACT_N];             /* SDL controller buttons, -1 = none */
                 int touchui;                /* on-screen touch controls: 0 auto (follow the last input device), 1 always, 2 never */
                 int intscale;               /* 0 smooth scaling, 1 whole-number scale with sharp pixels (desktop) */
                 int hdr;                    /* 0 SDR output, 1 ask for HDR output (desktop; only does anything on an HDR display) */
                 int ver;                    /* settings revision: 1 = the 3DS defaults to the analog touch pad */ } save_t;
static save_t SV;

/* The last kind of input the player used. It picks the prompt wording ("TOUCH SCREEN" or "PRESS ANYTHING") and, in AUTO, whether the
 * on-screen pad and pause button are drawn. Touch devices start as touch; desktops start as keyboard until a tap or click arrives. */
enum { IND_TOUCH, IND_MOUSE, IND_KEY, IND_PAD };
#if defined(__ANDROID__) || defined(__IPHONEOS__)
static int in_dev = IND_TOUCH;
#elif defined(__vita__) || defined(PNJ_N3DS) || defined(PNJ_DC) || defined(PNJ_NDS)
static int in_dev = IND_PAD;
#else
static int in_dev = IND_KEY;
#endif
static int touch_ui_on(void) { return SV.touchui == 1 || (SV.touchui == 0 && (in_dev == IND_TOUCH || in_dev == IND_MOUSE)); }
static const char *touchui_name(int m) { return m == 1 ? "TOUCH UI ON" : m == 2 ? "TOUCH UI OFF" : "TOUCH UI AUTO"; }
static void ctl_defaults(save_t *s) {
    static const int k[ACT_N][2] = { { SDLK_LEFT, SDLK_A }, { SDLK_RIGHT, SDLK_D }, { SDLK_UP, SDLK_W }, { SDLK_DOWN, SDLK_S }, { SDLK_RETURN, SDLK_SPACE }, { SDLK_ESCAPE, 0 }, { SDLK_P, 0 } };
    static const int b[ACT_N] = { SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                  SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_START };
    memcpy(s->key, k, sizeof s->key); memcpy(s->pad, b, sizeof s->pad);
}
/* For a rotated picture on a screen that has not been turned: make the arrows and D-pad point the way the game moves. With the picture
   turned right (top edge faces right) game-left is screen-up; turned left it is screen-down. Game-up is screen-right / screen-left. */
static void ctl_match_screen(save_t *s, int orient) {
    ctl_defaults(s); if (orient == 0) return;
    static const int uk[4] = { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT }, wk[4] = { SDLK_W, SDLK_S, SDLK_A, SDLK_D };
    static const int up[4] = { SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT };
    /* screen directions 0 up, 1 down, 2 left, 3 right; game LEFT, RIGHT, UP, DOWN take: */
    static const int right_turn[4] = { 0, 1, 3, 2 }, left_turn[4] = { 1, 0, 2, 3 };
    const int *m = orient == 2 ? right_turn : left_turn;
    for (int a = 0; a < 4; a++) {
        s->key[a][0] = uk[m[a]]; s->key[a][1] = wk[m[a]];
#ifndef __vita__
        s->pad[a] = up[m[a]];                       /* on the Vita the built-in pad is turned for you (act_of_pad) */
#endif
    }
}
static const char *orient_name(int o) { return o == 1 ? "TATE LEFT" : o == 2 ? "TATE RIGHT" : "STANDARD"; }
static int act_of_key(int k) {
    if (k == SDLK_KP_ENTER) k = SDLK_RETURN;
    if (k == SDLK_AC_BACK) return ACT_BACK;
    if (!k) return -1;
    for (int a = 0; a < ACT_N; a++) if (SV.key[a][0] == k || SV.key[a][1] == k) return a;
    return -1;
}
static int act_of_pad_raw(int b) { for (int a = 0; a < ACT_N; a++) if (SV.pad[a] == b) return a; return -1; }
static int act_of_pad(int b) {
#ifdef __vita__
    /* the Vita's own D-pad turns with the console: with the picture turned, press the way the game moves on the turned screen.
       Index 0 up, 1 down, 2 left, 3 right (the order of SDL_GAMEPAD_BUTTON_DPAD_*). */
    if (SV.orient && b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
        static const int tl[4] = { 3, 2, 0, 1 }, tr[4] = { 2, 3, 1, 0 };           /* picture turned left / right */
        b = SDL_GAMEPAD_BUTTON_DPAD_UP + (SV.orient == 2 ? tr : tl)[b - SDL_GAMEPAD_BUTTON_DPAD_UP];
    }
#endif
    return act_of_pad_raw(b);
}
static int act_held(int a) {                              /* any key bound to the action is down right now */
    const bool *st = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < 2; i++) if (SV.key[a][i]) { SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)SV.key[a][i], NULL); if (sc != SDL_SCANCODE_UNKNOWN && st[sc]) return 1; }
    return 0;
}
static void ctl_key_name(int k, char *out, size_t n) {
    if (!k) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%s", SDL_GetKeyName((SDL_Keycode)k));
    for (char *c = out; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    if (!strncmp(out, "KEYPAD ", 7)) { memmove(out + 3, out + 7, strlen(out + 7) + 1); memcpy(out, "KP ", 3); }
    if (!strncmp(out, "LEFT ", 5) && strlen(out) > 5) { memmove(out + 2, out + 5, strlen(out + 5) + 1); memcpy(out, "L ", 2); }
    else if (!strncmp(out, "RIGHT ", 6) && strlen(out) > 6) { memmove(out + 2, out + 6, strlen(out + 6) + 1); memcpy(out, "R ", 2); }
    if (strlen(out) > 9) out[9] = 0;
}
static const char *ctl_pad_name(int b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A"; case SDL_GAMEPAD_BUTTON_EAST: return "B"; case SDL_GAMEPAD_BUTTON_WEST: return "X"; case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_BACK: return "BACK"; case SDL_GAMEPAD_BUTTON_START: return "START";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L STICK"; case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R STICK";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L1"; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R1";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-UP"; case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-DOWN";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-LEFT"; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-RIGHT";
    default: return b < 0 ? "-" : "BTN"; }
}
static int orient_dirty = 0;                                /* set when the screen orientation changed; main resizes the window */
static int cap = -1;                                        /* controls screen: row * 3 + slot being rebound (slot 0, 1 keys, 2 pad), -1 none */
static char save_path[512];
static void save_apply(void) {
    sfx_on = SV.sfx_on && SV.sfx_vol > 0; mus_on = SV.mus_on && SV.mus_vol > 0; sfx_vol = SV.sfx_vol; mus_vol = SV.mus_vol * 0.8f;
    ctrl_analog = SV.analog; IN.snappy = SV.snappy;
}
static void save_flush_now(void) {
    if (!save_path[0]) return;
    FILE *f = fopen(save_path, "wb"); if (!f) return;
    fwrite(&SV, sizeof SV, 1, f); fclose(f);
}
#ifdef PNJ_DC
/* A write to the memory card stalls the game for a moment, so leaving a menu must not wait for one: the change is marked and written once things
 * have been quiet for a couple of seconds (save_tick), or right away at a loading screen or the end of a run (save_commit). */
static int save_dirty, save_idle;
static void save_write(void) { save_dirty = 1; save_idle = 0; }
static void save_commit(void) { if (save_dirty) { save_dirty = 0; save_flush_now(); } }
static void save_tick(void) { if (save_dirty && ++save_idle > 80) save_commit(); }
#else
static void save_write(void) { save_flush_now(); }
static void save_commit(void) {}
#endif
static void save_load(void) {
    memset(&SV, 0, sizeof SV);
    SV.magic = 0x504e4a31; SV.sfx_on = SV.mus_on = 1; SV.sfx_vol = 0.8f; SV.mus_vol = 0.7f; ctl_defaults(&SV);
    data_path(save_path, sizeof save_path, "pnj.sav");
    FILE *f = save_path[0] ? fopen(save_path, "rb") : NULL;
    if (f) { save_t t = SV; size_t n = fread(&t, 1, sizeof t, f);                                  /* a shorter (older) file leaves the new fields at their defaults */
             if (n >= offsetof(save_t, orient) && t.magic == 0x504e4a31) { SV = t; if (SV.orient < 0 || SV.orient > 2) SV.orient = 0; } fclose(f); }
#ifdef PNJ_N3DS
    if (SV.ver < 1) { SV.analog = 1; SV.ver = 1; }                                                 /* a stylus drag suits the 3DS better than the split digital pad */
#endif
    save_apply();
}

/* 3DS: N3C is true while a menu is drawn 1:1 on the touch screen (a window of 240 field rows starting at pnj_menu_win) instead of at half size */
#ifdef PNJ_N3DS
#define N3C (pnj_menu_win >= 0)
#define HISC 1.3            /* the HIGH label is hard to read after the 3DS shrinks the sheet */
#else
#define N3C 0
#define HISC 1.0
#endif
/* ---------- drawing helpers ---------- */
static void sprdst(SDL_Renderer *r, int id, double dx, double dy, double dw, double dh, int rgb) {
    const pnj_sprite *s = spr_at(id);
    SDL_Texture *t = get_tex(r, s->tex); if (!t) return;
    double k = (double)TEXW[s->tex] / s->tw;
    SDL_FRect src = { (int)(s->x * k + 0.5), (int)(s->y * k + 0.5), (int)(s->w * k + 0.5), (int)(s->h * k + 0.5) };
    SDL_FRect dst = { (float)dx, (float)dy, (float)dw, (float)dh };
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureAlphaMod(t, 255);
    SDL_SetTextureColorMod(t, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    SDL_RenderTexture(r, t, &src, &dst);
}
static void pill(SDL_Renderer *r, double x, double y, double w, int rgb) {
    sprdst(r, 62, x, y, 19, 38, rgb);
    if (w > 38) sprdst(r, 63, x + 19, y, w - 38, 38, rgb);
    sprdst(r, 64, x + w - 19, y, 19, 38, rgb);
}
/* the game's dotted font: 16x6 grid of 32px cells starting at ' ' in sysparts (tex 2). align 0 centre, 1 left, 2 right */
static void ftext(SDL_Renderer *r, const char *t, double x, double y, double size, int rgb, int align, double alpha) {
    SDL_Texture *tx = get_tex(r, 2); if (!tx) return;
    double k = (double)TEXW[2] / 512.0, cell = size * 1.15, adv = cell * 0.7, w = strlen(t) * adv;
#ifdef PNJ_N3DS
    if (FONTLITE && r->sc < 0.75f) {                                  /* half-size menus use a pre-shrunk font: pick the level closest to the drawn cell size */
        double dc = cell * r->sc;
        if (FONTLITE8 && dc < 11.3) { tx = FONTLITE8; k = 0.25; } else if (dc < 22.6) { tx = FONTLITE; k = 0.5; }
    }
#endif
    double px = align == 1 ? x : align == 2 ? x - w : x - w / 2, py = y - cell * 0.78;
    SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tx, (Uint8)(alpha < 0 ? 0 : alpha > 1 ? 255 : alpha * 255));
    SDL_SetTextureColorMod(tx, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    for (const char *c = t; *c; c++) {
        int code = toupper((unsigned char)*c);
        if (code > 32 && code < 128) {
            int i = code - 32, cx = i % 16, cy = i / 16;
            SDL_FRect src = { (int)(cx * 32 * k + 0.5), (int)(cy * 32 * k + 0.5), (int)(32 * k + 0.5), (int)(32 * k + 0.5) };
            SDL_FRect dst = { (float)(px - (cell - adv) / 2), (float)py, (float)cell, (float)cell };
            SDL_RenderTexture(r, tx, &src, &dst);
        }
        px += adv;
    }
}
static void fill_rect(SDL_Renderer *r, double x, double y, double w, double h, int rgb, int a) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, (Uint8)a);
    SDL_FRect f = { (float)x, (float)y, (float)w, (float)h }; SDL_RenderFillRect(r, &f);
}
static void shade(SDL_Renderer *r, int a) { fill_rect(r, 0, 0, PNJ_W, PNJ_H, 0, a); }
static void digits_right(SDL_Renderer *r, int v, double xr, double ytop) {
    char b[16]; snprintf(b, sizeof b, "%d", v < 0 ? 0 : v);
    draw_digits(r, v, xr - 20.0 * strlen(b), ytop);
}

/* ---------- hit regions (rebuilt every frame) ---------- */
enum { H_NONE, H_TITLE, H_PLAY, H_RECORD, H_OPTION, H_BACK, H_PREV, H_NEXT, H_START, H_SFXT, H_MUST, H_SFXS, H_MUSS, H_CTRL, H_SNAP,
       H_P_OPTION, H_P_EXIT, H_P_RESUME, H_OVER, H_HELP, H_ACH, H_P_HELP, H_QUIT, H_P_QUIT, H_ORIENT, H_KEYS, H_RESET, H_MATCH, H_TOUCHUI, H_SCALE, H_HDR, H_O_SND, H_O_VID, H_O_CTL, H_FS, H_ABSWAP, H_KEY0 };   /* H_KEY0 + row * 3 + slot: a binding box */
#if !defined(__ANDROID__) && !defined(__IPHONEOS__) && !defined(__vita__) && !defined(PNJ_N3DS) && !defined(PNJ_DC) && !defined(PNJ_NDS)
#define PNJ_HAS_QUIT 1      /* desktop only: phones and consoles quit through the system */
#define PNJ_DESKTOP 1
#endif
#ifdef __vita__
#define PNJ_VITA 1
#endif
#if defined(PNJ_DESKTOP) || defined(PNJ_VITA) || defined(PNJ_DC)
#define PNJ_HAS_VIDEO 1     /* the Video screen: desktop has fullscreen, rotation, scale and HDR; the Vita has rotation and scale; the Dreamcast has rotation (tate) */
#endif
typedef struct { int x, y, w, h, id; } hit_t;
static hit_t hits[40]; static int nhits;
static void hit_add(int x, int y, int w, int h, int id) { if (nhits < 40) hits[nhits++] = (hit_t){ x, y, w, h, id }; }
static void btn(SDL_Renderer *r, const char *label, int x, int y, int w, int h, int id, int fill, double size, int disabled) {
    double py = y + (h - 38) / 2.0;
    if (!disabled) hit_add(x, y, w, h, id);
    pill(r, x, py, w, disabled ? 0x444444 : fill);
    ftext(r, label, x + w / 2.0, py + 18.5 + size * 0.42, size, disabled ? 0x777777 : 0xffffff, 0, 1);   /* glyph centre = baseline - 0.42 size */
}
static void backbtn(SDL_Renderer *r) {
#ifdef PNJ_N3DS
    if (N3C) { SPR(r, 69, 40, pnj_menu_win + 212); hit_add(0, pnj_menu_win + 182, 96, 58, H_BACK); return; }
#endif
    SPR(r, 69, 40, 448); hit_add(0, 410, 96, 70, H_BACK);
}
static void slider(SDL_Renderer *r, int x, int y, int w, double val, int on, int id) {
    hit_add(x - 8, y - 14, w + 16, 28, id);
    fill_rect(r, x, y - 3, w, 6, on ? 0x333333 : 0x222222, 255);
    fill_rect(r, x, y - 3, fmax(6, w * val), 6, on ? 0xffd400 : 0x665a00, 255);
    fill_rect(r, x + w * val - 8, y - 8, 16, 16, on ? 0xffffff : 0x888888, 255);
}

/* ---------- state ---------- */
enum { UI_TITLE, UI_MENU, UI_STAGES, UI_RECORDS, UI_OPTION, UI_OVER, UI_PLAY, UI_HELP, UI_ACH, UI_KEYS, UI_OPT_SOUND, UI_OPT_VIDEO, UI_OPT_CTRL };
static int is_opt(int m) { return m == UI_OPTION || m == UI_KEYS || m == UI_OPT_SOUND || m == UI_OPT_VIDEO || m == UI_OPT_CTRL; }     /* the Options screens and their sub-screens */
static int fs_state, fs_req;                         /* fullscreen: main.c keeps fs_state current and acts on fs_req */
static int ui_mode = UI_TITLE, ui_L = 0, sel_stage = 0, title_start = 0, rec_page = 0, opt_back = UI_MENU;
static int over_score = 0, over_new = 0, help_page = 0, ach_page = 0, help_back = UI_MENU;
static int over_ach[90], over_nach = 0;
static double title_x0[11];
static const int TITLE_X[10] = { 10, 32, 66, 102, 119, 153, 175, 207, 239, 275 };
static const char *const STAGE_NAMES[8] = { "PAC-MAN", "DIG DUG", "RALLY-X", "GALAGA", "MAPPY", "PAC-LAND", "THE TOWER OF DRUAGA", "XEVIOUS" };

static void ui_enter(int mode) {
    int keep = mode == UI_OVER || (is_opt(mode) && opt_back == UI_PLAY) || (mode == UI_HELP && help_back == UI_PLAY);
    ui_mode = mode;
    if (mode == UI_TITLE) {
        title_start = ui_L;
        for (int i = 0; i < 10; i++) title_x0[i] = ((rand() % 320) - 160) * 2;
        title_x0[10] = 0;
    }
    if (mode != UI_PLAY && !keep) { pnj_new(&G, 1, sel_stage); paused = 0; touch_dir = 0; }
}
static void start_stage(int stage) { sel_stage = stage; new_game((uint32_t)SDL_GetTicks() + 1, stage); ui_mode = UI_PLAY; }

static void draw_logo(SDL_Renderer *r, int t, int final) {
    for (int i = 0; i < 10; i++) {
        double c0 = i * 90 + 660, cnt = final ? 0 : c0 - 15.0 * t; if (cnt < 0) cnt = 0;
        double dx = title_x0[i] * cnt / c0, dy = cnt <= 0 ? 0 : cnt > 180 ? cnt - 180 : -sin(RADIANS(cnt)) * 40;
        const pnj_sprite *sp = spr_at(4 + i);
        SPR(r, 4 + i, TITLE_X[i] + sp->w / 2.0 + dx, 36 + sp->h / 2.0 + dy);
    }
    if (final || 10 * 90 + 660 - 15.0 * t <= 0) SPR(r, 14, 296 + 12, 76 + 6.5);
}
static void blink_text(SDL_Renderer *r, const char *s, double y, double size) {
    double a = sin(RADIANS((ui_L % 30) * 6)); if (a < 0) a = 0;
    ftext(r, s, 160, y, size, 0xffffff, 0, a);
}

/* ---------- help and achievements ---------- */
/* The nine Help pages are the game's own strings: they are read from lang/language.eng in the pack (the last 18 lines: title, body, x9).
 * Only the sprite shown on each page is chosen here. */
static const int HELP_SPR[9] = { 18, 19, 106, 105, 107, 115, 116, 117, 0 };
static struct { char title[48]; char body[256]; int spr; } HELP[9];
static void help_load(void) {
    for (int i = 0; i < 9; i++) { snprintf(HELP[i].title, sizeof HELP[i].title, "HELP"); HELP[i].body[0] = 0; HELP[i].spr = HELP_SPR[i]; }
    size_t n; unsigned char *b = read_asset("lang/language.eng", &n); if (!b) return;
    char *txt = (char *)malloc(n + 1); if (!txt) { free(b); return; } memcpy(txt, b, n); txt[n] = 0; free(b);
    char *ln[1024]; int nl = 0;
    for (char *p = txt; p && *p && nl < 1024; ) { char *e = strchr(p, '\n'); if (e) *e = 0; size_t l = strlen(p); if (l && p[l - 1] == '\r') p[l - 1] = 0; ln[nl++] = p; p = e ? e + 1 : NULL; }
    int at = -1; for (int i = 0; i < nl; i++) if (!SDL_strcasecmp(ln[i], "Game Rules")) at = i;
    if (at >= 0 && at + 18 <= nl) for (int i = 0; i < 9; i++) {
        char *t = HELP[i].title; snprintf(t, sizeof HELP[i].title, "%s", ln[at + 2 * i]); for (; *t; t++) *t = (char)toupper((unsigned char)*t);
        const char *src = ln[at + 2 * i + 1]; char *o = HELP[i].body, *end = o + sizeof HELP[i].body - 1;
        for (; *src && o < end; src++) {
            if (src[0] == '\\' && src[1] == 'n') { while (o > HELP[i].body && o[-1] == ' ') o--; *o++ = '\n'; src++; } else *o++ = *src;
        }
        while (o > HELP[i].body && o[-1] == ' ') o--;
        *o = 0;
    }
    free(txt);
}
static void ach_name(int i, char *b, size_t n) {
    if (i < 80) snprintf(b, n, "STAGE%d JUMP %d", i / 10 + 1, (i % 10 + 1) * 10000);
    else snprintf(b, n, "TOTAL %d", (i - 79) * 100000);
}
static int ach_done_with(int i, const int *tops, int total) { return i < 80 ? tops[i / 10] >= (i % 10 + 1) * 10000 : total >= (i - 79) * 100000; }
static int ach_done(int i) { int tops[8]; for (int k = 0; k < 8; k++) tops[k] = SV.top[k][0]; return ach_done_with(i, tops, SV.total); }
static void fit_text(SDL_Renderer *r, const char *t, double x, double y, double size, double maxw, int rgb, int align) {
    double w = strlen(t) * size * 1.15 * 0.7; if (w > maxw) size *= maxw / w;
    ftext(r, t, x, y, size, rgb, align, 1);
}
static void draw_help(SDL_Renderer *r) {
    shade(r, 165);
    const char *ttl = HELP[help_page].title; fit_text(r, ttl, 160, 66, 26, 300, 0xffe07a, 0);
    int sp = HELP[help_page].spr;
    if (sp) spr(r, sp, 0, 0, 0, 160, 130, 0, sp == 18 || sp == 19 ? 1.6 : 1.3, 1, 0, 1, 1, 1);
    else for (int i = 0; i < 4; i++) spr(r, 151 + i, 0, 0, 0, 80 + i * 53, 130, 0, 1.2, 1, 0, 1, 1, 1);
    char buf[256]; snprintf(buf, sizeof buf, "%s", HELP[help_page].body);
    int ln = 0; for (char *p = strtok(buf, "\n"); p; p = strtok(NULL, "\n"), ln++) fit_text(r, p, 160, 222 + ln * 22, 9, 308, 0xffffff, 0);
    char b[16]; snprintf(b, sizeof b, "%d / 9", help_page + 1); ftext(r, b, 160, 405, 11, 0xaaaaaa, 0, 1);
    SPR(r, 66, 223, 448); hit_add(186, 410, 70, 70, H_PREV);
    SPR(r, 67, 289, 448); hit_add(256, 410, 64, 70, H_NEXT);
    backbtn(r);
}
static void draw_ach(SDL_Renderer *r) {
    shade(r, 165);
    int got = 0; for (int i = 0; i < 90; i++) got += ach_done(i);
    char b[48];
    ftext(r, "ACHIEVEMENTS", 160, 30, 16, 0xffe07a, 0, 1);
    snprintf(b, sizeof b, "%d / 90", got); ftext(r, b, 160, 50, 12, 0xffffff, 0, 1);
    fit_text(r, ach_page < 8 ? STAGE_NAMES[ach_page] : "TOTAL DOTS", 160, 80, 21, 300, 0xffa200, 0);
    for (int k = 0; k < 10; k++) {
        int i = ach_page * 10 + k, y = 96 + k * 32, d = ach_done(i);
        spr(r, d ? 72 : 70, 0, 0, 0, 28, y + 4, 0, d ? 0.5 : 0.42, d ? 1 : 0.45, 0, 1, 1, 1);
        ach_name(i, b, sizeof b); ftext(r, b, 56, y + 14, 12, d ? 0xffffff : 0x777777, 1, 1);
    }
    if (ach_page == 8) { snprintf(b, sizeof b, "LIFETIME: %d DOTS", SV.total); ftext(r, b, 160, 424, 11, 0xaaaaaa, 0, 1); }
    SPR(r, 66, 223, 448); hit_add(186, 410, 70, 70, H_PREV);
    SPR(r, 67, 289, 448); hit_add(256, 410, 64, 70, H_NEXT);
    backbtn(r);
}

static double tap_x = -99, tap_y = -99;
static int kb_on = 0, kb_cur = -1, kb_sig = -1;      /* keyboard/gamepad focus: kb_cur indexes hits[] of the current frame, -1 = none yet */
static int kb_id = -1;                               /* id of the control the keyboard/pad has focused (-1 = none), refreshed every frame in draw_hits */
static int kb_paged(void) { return !paused && (ui_mode == UI_STAGES || ui_mode == UI_RECORDS || ui_mode == UI_HELP || ui_mode == UI_ACH); }
static void kb_track(void) {
    kb_id = kb_on && !kb_paged() && kb_cur >= 0 && kb_cur < nhits && kb_sig == ui_mode * 2 + (paused ? 1 : 0) ? hits[kb_cur].id : -1;
}
/* Focus uses the game's own highlight: the lit control is the yellow pill (others lavender), or a yellow row heading on the options screen.
   With no keyboard/pad focus the screen's default stays lit, as on touch. */
static int menu_fill(int id, int dflt) { return kb_id < 0 ? dflt : id == kb_id ? 0xffe07a : 0xb4b4d2; }
static int row_rgb(int id, int rgb) { return kb_id == id ? 0xffe07a : rgb; }
static void draw_hits(SDL_Renderer *r) {   /* dev aid: PNJ_HITS=1 outlines every hit region and marks the last tap */
    kb_track();
    if (!getenv("PNJ_HITS")) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    for (int i = 0; i < nhits; i++) { SDL_FRect f = { (float)hits[i].x, (float)hits[i].y, (float)hits[i].w, (float)hits[i].h }; SDL_RenderRect(r, &f); }
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    SDL_RenderLine(r, (float)tap_x - 10, (float)tap_y, (float)tap_x + 10, (float)tap_y); SDL_RenderLine(r, (float)tap_x, (float)tap_y - 10, (float)tap_x, (float)tap_y + 10);
    char b[96]; snprintf(b, sizeof b, "tap %d,%d", (int)tap_x, (int)tap_y);
    ftext(r, b, 160, 8, 8, 0x66ff66, 0, 1);
    ftext(r, PNJ_VERSION, 160, 8, 20, 0x66ff66, 0, 1);
}
static void draw_menu(SDL_Renderer *r) {
    nhits = 0;
    int scene = ui_mode == UI_OVER || (is_opt(ui_mode) && opt_back == UI_PLAY) || (ui_mode == UI_HELP && help_back == UI_PLAY);
    if (scene) { int kid = kb_id; render(r); nhits = 0; kb_id = kid; }   /* render() refreshes the focus against an empty hit list; keep the one from last frame */
    else {
        G.stage = sel_stage; G.L = ui_L; G.cam594 = (fx)(-ui_L * 4); G.peak = 0; G.p.power = 0;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
        draw_bg(r);
    }
    switch (ui_mode) {
    case UI_TITLE: {
        int id = 126 + 3 * sel_stage;
        for (int i = 0; i < 10; i++) SPR(r, id, (i % 2 ? 260 : 60) + 35, 22 + 54 * i + 12);
        int cyc = 40, k = 9 - ((ui_L / cyc) % 9); double u = (double)(ui_L % cyc) / cyc;
        double x0 = (k % 2 ? 260 : 60) + 35, x1 = ((k - 1) % 2 ? 260 : 60) + 35, y0 = 22 + 54 * k - 17, y1 = 22 + 54 * (k - 1) - 17;
        double uu = u * 1.15 > 1 ? 1 : u * 1.15, uu2 = uu + 0.03 > 1 ? 1 : uu + 0.03;
        double px = x0 + (x1 - x0) * uu, py = y0 + (y1 - y0) * uu - sin(uu * 3.14159265) * 70;
        double qx = x0 + (x1 - x0) * uu2, qy = y0 + (y1 - y0) * uu2 - sin(uu2 * 3.14159265) * 70;
        static const int fr[4] = { 0, 1, 2, 1 };
        spr(r, 92, fr[(ui_L >> 1) % 4], 0, 0, px, py, atan2(qx - px, -(qy - py)) * 180 / 3.14159265, 1, 1, 0, 1, 1, 1);
        int t = ui_L - title_start;
        draw_logo(r, t, 0);
        if (t > 40) { blink_text(r, in_dev == IND_TOUCH ? "TOUCH SCREEN!!" : "PRESS ANYTHING!!", 248, in_dev == IND_TOUCH ? 22 : 20); SPR(r, 15, 160, 415); SPR(r, 16, 160, 450); }
        hit_add(0, 0, PNJ_W, PNJ_H, H_TITLE);
        break; }
    case UI_MENU: {
        if (!N3C) draw_logo(r, 0, 1);                           /* 3DS full-size menu: the logo is on the top screen */
#ifdef PNJ_HAS_QUIT
        static const char *const items[6] = { "PLAY GAME", "RECORD", "OPTION", "HELP", "ACHIEVEMENTS", "QUIT TO DESKTOP" };
        static const int ids[6] = { H_PLAY, H_RECORD, H_OPTION, H_HELP, H_ACH, H_QUIT };
        enum { NITEMS = 6, ITEM_Y = 176, ITEM_DY = 40 };
#else
        static const char *const items[5] = { "PLAY GAME", "RECORD", "OPTION", "HELP", "ACHIEVEMENTS" };
        static const int ids[5] = { H_PLAY, H_RECORD, H_OPTION, H_HELP, H_ACH };
        enum { NITEMS = 5, ITEM_Y = 180, ITEM_DY = 45 };
#endif
        for (int i = 0; i < NITEMS; i++) btn(r, items[i], 10, ITEM_Y + ITEM_DY * i - 19, 300, 38, ids[i], menu_fill(ids[i], i == 0 ? 0xffe07a : 0xb4b4d2), 16, 0);
        spr(r, 15, 0, 0, 0, 160, 436, 0, 0.8, 1, 0, 1, 1, 1); spr(r, 16, 0, 0, 0, 160, 466, 0, 0.8, 1, 0, 1, 1, 1);
        break; }
    case UI_STAGES: {
        shade(r, 140);
        int ready = stage_ready(sel_stage); char b[16];
        ftext(r, "SELECT STAGE THEME", 160, 26, 11, 0xaaaaaa, 0, 1);
        snprintf(b, sizeof b, "STAGE%d", sel_stage + 1); ftext(r, b, 160, 62, 24, 0x9966ff, 0, 1);
        ftext(r, STAGE_NAMES[sel_stage], 160, 100, sel_stage == 6 ? 17 : 22, 0xffffff, 0, 1);
        spr(r, 83 + sel_stage, 0, 0, 0, 160, 210, 0, 1, ready ? 1 : 0.3, 0, 1, 1, 1);
        spr(r, 61, 0, 0, 0, 59, 321, 0, HISC, 1, 0, 1, 1, 1); digits_right(r, SV.top[sel_stage][0], 240, 305); SPR(r, 59, 268, 330);
        SPR(r, 66, 34, 210); hit_add(0, 170, 66, 80, H_PREV);
        SPR(r, 67, 286, 210); hit_add(254, 170, 66, 80, H_NEXT);
        spr(r, 68, 0, 0, 0, 160, 410, 0, 1, ready ? 1 : 0.35, 0, 1, 1, 1); hit_add(92, 372, 136, 76, H_START);
        backbtn(r);
        break; }
    case UI_RECORDS: {
        shade(r, 150);
        char b[24];
        ftext(r, "RECORD", 160, 48, 22, 0xffe07a, 0, 1);
        snprintf(b, sizeof b, "STAGE%d", rec_page + 1); ftext(r, b, 160, 119, 20, 0xffa200, 0, 1);
        for (int i = 0; i < 5; i++) {
            int y = 161 + 45 * i; snprintf(b, sizeof b, "NO.%d", i + 1);
            ftext(r, b, 8, y + 18, 15, 0x32bce4, 1, 1);
            digits_right(r, SV.top[rec_page][i], 268, y + 2); spr(r, 59, 0, 0, 0, 300, y + 20, 0, 0.8, 1, 0, 1, 1, 1);
        }
        SPR(r, 66, 223, 448); hit_add(186, 410, 70, 70, H_PREV);
        SPR(r, 67, 289, 448); hit_add(256, 410, 64, 70, H_NEXT);
        backbtn(r);
        break; }
    case UI_OPTION: {                                           /* the hub: pick a group */
        shade(r, scene ? 200 : 155);
        if (N3C) {
            ftext(r, "OPTION", 160, 34, 22, 0xffe07a, 0, 1);
            btn(r, "SOUND", 60, 58, 200, 52, H_O_SND, menu_fill(H_O_SND, 0xb4b4d2), 18, 0);
            btn(r, "CONTROLS", 60, 122, 200, 52, H_O_CTL, menu_fill(H_O_CTL, 0xb4b4d2), 18, 0);
            backbtn(r);
            break;
        }
        ftext(r, "OPTION", 160, 48, 22, 0xffe07a, 0, 1);
#if defined(PNJ_HAS_VIDEO) && !defined(PNJ_DC)
        const int rows = 3;
#else
        const int rows = 2;                                     /* the Dreamcast has no touch screen, so its Controls page (virtual pad, touch UI) is left out */
#endif
        int y = rows == 3 ? 120 : 150;
        btn(r, "SOUND", 60, y, 200, 56, H_O_SND, menu_fill(H_O_SND, 0xb4b4d2), 18, 0); y += 80;
#ifdef PNJ_HAS_VIDEO
        btn(r, "VIDEO", 60, y, 200, 56, H_O_VID, menu_fill(H_O_VID, 0xb4b4d2), 18, 0); y += 80;
#endif
#ifndef PNJ_DC
        btn(r, "CONTROLS", 60, y, 200, 56, H_O_CTL, menu_fill(H_O_CTL, 0xb4b4d2), 18, 0);
#endif
        backbtn(r);
        break; }
    case UI_OPT_SOUND: {
        shade(r, scene ? 200 : 155);
        ftext(r, "SOUND", 160, 48, 22, 0xffe07a, 0, 1);
        ftext(r, "SOUND EFFECTS", 20, 124, 11, row_rgb(H_SFXT, 0xaaaaaa), 1, 1);
        btn(r, SV.sfx_on ? "ON" : "OFF", 14, 126, 70, 38, H_SFXT, SV.sfx_on ? 0xffe07a : 0x777777, 13, 0);
        slider(r, 110, 145, 180, SV.sfx_vol, SV.sfx_on, H_SFXS);
        ftext(r, "BACKGROUND MUSIC", 20, 214, 11, row_rgb(H_MUST, 0xaaaaaa), 1, 1);
        btn(r, SV.mus_on ? "ON" : "OFF", 14, 216, 70, 38, H_MUST, SV.mus_on ? 0xffe07a : 0x777777, 13, 0);
        slider(r, 110, 235, 180, SV.mus_vol, SV.mus_on, H_MUSS);
        backbtn(r);
        break; }
    case UI_OPT_VIDEO: {                                        /* desktop only; the hub has no entry for it elsewhere */
        shade(r, scene ? 200 : 155);
        ftext(r, "VIDEO", 160, 48, 22, 0xffe07a, 0, 1);
#ifdef PNJ_HAS_VIDEO
        int vy = 98;
#ifdef PNJ_DESKTOP
        ftext(r, "FULLSCREEN", 20, vy + 21, 11, row_rgb(H_FS, 0xaaaaaa), 1, 1);
        btn(r, fs_state ? "ON" : "OFF", 150, vy, 164, 38, H_FS, fs_state ? 0xffe07a : menu_fill(H_FS, 0xb4b4d2), 12, 0);
        vy += 60;
#endif
        ftext(r, "SCREEN", 20, vy + 11, 11, row_rgb(H_ORIENT, 0xaaaaaa), 1, 1);
        ftext(r, "ROTATION", 20, vy + 29, 11, row_rgb(H_ORIENT, 0xaaaaaa), 1, 1);
        btn(r, orient_name(SV.orient), 150, vy, 164, 38, H_ORIENT, menu_fill(H_ORIENT, 0xb4b4d2), 12, 0);
        vy += 60;
#ifdef PNJ_DC
        ftext(r, "TATE TURNS THE PICTURE A QUARTER TURN.", 160, vy + 6, 8, 0xcccccc, 0, 1);
        ftext(r, "STAND YOUR TV ON ITS SIDE: LEFT IF THE", 160, vy + 22, 8, 0xcccccc, 0, 1);
        ftext(r, "TOP OF THE GAME IS ON ITS LEFT, ELSE RIGHT.", 160, vy + 38, 8, 0xcccccc, 0, 1);
#else
        ftext(r, "SCALE", 20, vy + 21, 11, row_rgb(H_SCALE, 0xaaaaaa), 1, 1);
        btn(r, SV.intscale ? "INT SCALE" : "SMOOTH", 150, vy, 164, 38, H_SCALE, menu_fill(H_SCALE, 0xb4b4d2), 12, 0);
        vy += 60;
#endif
#ifdef PNJ_DESKTOP
        ftext(r, "HDR", 20, vy + 21, 11, row_rgb(H_HDR, 0xaaaaaa), 1, 1);
        btn(r, SV.hdr ? "ON" : "OFF", 150, vy, 164, 38, H_HDR, SV.hdr ? 0xffe07a : menu_fill(H_HDR, 0xb4b4d2), 12, 0);
        { char hb[40];
          if (!SV.hdr) snprintf(hb, sizeof hb, "SDR OUTPUT");
          else if (hdr_active) snprintf(hb, sizeof hb, "ACTIVE. PEAK X%.1f", (double)hdr_headroom);
          else snprintf(hb, sizeof hb, "NO HDR DISPLAY FOUND");
          ftext(r, hb, 232, vy + 55, 8, 0xcccccc, 0, 1); }
#endif
#endif
        backbtn(r);
        break; }
    case UI_OPT_CTRL: {
        shade(r, scene ? 200 : 155);
        ftext(r, "CONTROLS", 160, 48, 22, 0xffe07a, 0, 1);
        ftext(r, "TOUCH UI", 20, 119, 11, row_rgb(H_TOUCHUI, 0xaaaaaa), 1, 1);
        btn(r, touchui_name(SV.touchui), 150, 98, 164, 38, H_TOUCHUI, menu_fill(H_TOUCHUI, 0xb4b4d2), 10, 0);
        ftext(r, SV.analog ? "ANALOG PAD" : "DIGITAL PAD", 30, 191, 15, 0xffffff, 1, 1);
        SPR(r, 66, 204, 184); hit_add(172, 162, 62, 44, H_CTRL);
        SPR(r, 67, 269, 184); hit_add(238, 162, 62, 44, H_CTRL);
        if (SV.analog) { ftext(r, "ANALOG-STYLE VIRTUAL CONTROL PAD.", 14, 228, 8, 0xcccccc, 1, 1); ftext(r, "THE SLIDE BAR RESPONDS ANYWHERE.", 14, 244, 8, 0xcccccc, 1, 1); }
        else {
            ftext(r, "DIGITAL-STYLE VIRTUAL CONTROL PAD.", 14, 228, 8, 0xcccccc, 1, 1);
            btn(r, SV.snappy ? "ON" : "OFF", 14, 262, 70, 38, H_SNAP, SV.snappy ? 0xffe07a : 0x777777, 13, 0);
            ftext(r, "SNAPPY DIGITAL RESPONSE", 96, 285, 10, row_rgb(H_SNAP, 0xffffff), 1, 1);
        }
#if defined(PNJ_DESKTOP) || defined(PNJ_VITA)
        btn(r, "KEYS", 60, 340, 200, 44, H_KEYS, menu_fill(H_KEYS, 0xb4b4d2), 13, 0);
#endif
#if defined(PNJ_N3DS) || defined(PNJ_NDS)
        btn(r, SV.pad[ACT_OK] == SDL_GAMEPAD_BUTTON_EAST ? "B: OK  A: BACK" : "A: OK  B: BACK", 60, 340, 200, 44, H_ABSWAP, menu_fill(H_ABSWAP, 0xb4b4d2), 11, 0);
#endif
        backbtn(r);
        break; }
    case UI_KEYS: {
        static const char *const names[ACT_N] = { "LEFT", "RIGHT", "MENU UP", "MENU DOWN", "SELECT", "BACK", "PAUSE" };
        shade(r, scene ? 215 : 170);
        ftext(r, "KEYS", 160, 30, 20, 0xffe07a, 0, 1);
        ftext(r, "KEY", 131, 66, 8, 0xaaaaaa, 0, 1); ftext(r, "KEY", 205, 66, 8, 0xaaaaaa, 0, 1); ftext(r, "PAD", 279, 66, 8, 0xaaaaaa, 0, 1);
        for (int a = 0; a < ACT_N; a++) {
            int y = 80 + 42 * a; char nb[16];
            ftext(r, names[a], 10, y + 24, 9, 0xffffff, 1, 1);
            for (int sl = 0; sl < 3; sl++) {
                int id = H_KEY0 + a * 3 + sl, on = cap == a * 3 + sl;
                if (on) snprintf(nb, sizeof nb, "PRESS..."); else if (sl < 2) ctl_key_name(SV.key[a][sl], nb, sizeof nb); else snprintf(nb, sizeof nb, "%s", ctl_pad_name(SV.pad[a]));
                btn(r, nb, 94 + 74 * sl, y, 70, 38, id, on ? 0xffe07a : menu_fill(id, 0xb4b4d2), 8, 0);
            }
        }
        ftext(r, "TAP A BOX, THEN PRESS THE KEY OR PAD BUTTON.", 160, 386, 7, 0xaaaaaa, 0, 1);
        ftext(r, "ESC CANCELS. A KEY USED TWICE MOVES TO THE NEW SPOT.", 160, 399, 7, 0xaaaaaa, 0, 1);
        backbtn(r);
        btn(r, "DEFAULTS", 104, 424, 100, 38, H_RESET, menu_fill(H_RESET, 0xb4b4d2), 10, 0);
        btn(r, "MATCH SCREEN", 210, 424, 106, 38, H_MATCH, menu_fill(H_MATCH, 0xb4b4d2), 9, 0);
        break; }
    case UI_HELP: draw_help(r); break;
    case UI_ACH: draw_ach(r); break;
    case UI_OVER: {
        shade(r, 150);
        blink_text(r, "GAME OVER", 150, 30);
        ftext(r, "SCORE", 160, 205, 16, 0xaaaaaa, 0, 1);
        char b[16]; snprintf(b, sizeof b, "%d", over_score);
        draw_digits(r, over_score, 160 - 10.0 * strlen(b), 215);
        if (over_new) ftext(r, "NEW BEST!", 160, 290, 22, 0xffe07a, 0, 1);
        else { spr(r, 61, 0, 0, 0, 100, 300, 0, HISC, 1, 0, 1, 1, 1); digits_right(r, SV.top[sel_stage][0], 240, 288); }
        if (over_nach) {
            ftext(r, "ACHIEVEMENT UNLOCKED!", 160, 332, 13, 0xffe07a, 0, 1);
            for (int i = 0; i < over_nach && i < 3; i++) { char nb[40]; ach_name(over_ach[i], nb, sizeof nb); ftext(r, nb, 160, 354 + i * 18, 11, 0xffffff, 0, 1); }
            if (over_nach > 3) { char nb[24]; snprintf(nb, sizeof nb, "+%d MORE", over_nach - 3); ftext(r, nb, 160, 408, 10, 0xaaaaaa, 0, 1); }
        }
        if (ui_L % 60 < 40) ftext(r, in_dev == IND_TOUCH ? "TOUCH SCREEN" : "PRESS ANYTHING", 160, 440, 16, 0xffffff, 0, 1);
        hit_add(0, 0, PNJ_W, PNJ_H, H_OVER);
        break; }
    }
    draw_hits(r);
}

/* pause overlay, drawn over the game by render() */
static void draw_pause(SDL_Renderer *r) {
    shade(r, 160);
    ftext(r, "PAUSE", 160, N3C ? 136 : 130, 30, 0xffffff, 0, 1);
#ifdef PNJ_HAS_QUIT
    enum { Y0 = 180, DY = 40 };
#else
    enum { Y0 = 195, DY = 45 };
#endif
    int y = N3C ? 182 : Y0; const int dy = N3C ? 42 : DY;
    btn(r, "HELP", 60, y - 19, 200, 38, H_P_HELP, menu_fill(H_P_HELP, 0xb4b4d2), 16, 0); y += dy;
    btn(r, "OPTION", 60, y - 19, 200, 38, H_P_OPTION, menu_fill(H_P_OPTION, 0xb4b4d2), 16, 0); y += dy;
    btn(r, "EXIT", 60, y - 19, 200, 38, H_P_EXIT, menu_fill(H_P_EXIT, 0xb4b4d2), 16, 0); y += dy;
#ifdef PNJ_HAS_QUIT
    btn(r, "QUIT TO DESKTOP", 60, y - 19, 200, 38, H_P_QUIT, menu_fill(H_P_QUIT, 0xb4b4d2), 14, 0); y += dy;
#endif
    btn(r, "RESUME", 60, y - 19, 200, 38, H_P_RESUME, menu_fill(H_P_RESUME, 0xffe07a), 16, 0);
}

/* ---------- touch handling for menus ---------- */
static int drag_id = H_NONE, drag_x, drag_w;
static void apply_slider(int id, double lx) {
    double v = (lx - drag_x) / drag_w; v = v < 0 ? 0 : v > 1 ? 1 : v;
    if (id == H_SFXS) { SV.sfx_vol = (float)v; if (v > 0) SV.sfx_on = 1; } else { SV.mus_vol = (float)v; if (v > 0) SV.mus_on = 1; }
    save_apply();
}
/* exact hit first (topmost wins); a tap that lands just outside every button snaps to the nearest one within 14 px */
static int hit_find(double lx, double ly) {
    for (int i = nhits - 1; i >= 0; i--) { hit_t *h = &hits[i]; if (lx >= h->x && lx < h->x + h->w && ly >= h->y && ly < h->y + h->h) return i; }
    int best = -1; double bd = 14;
    for (int i = 0; i < nhits; i++) {
        hit_t *h = &hits[i]; if (h->id == H_TITLE || h->id == H_OVER) continue;
        double dx = lx < h->x ? h->x - lx : lx > h->x + h->w ? lx - h->x - h->w : 0, dy = ly < h->y ? h->y - ly : ly > h->y + h->h ? ly - h->y - h->h : 0;
        double d = sqrt(dx * dx + dy * dy); if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static void ui_touch(double lx, double ly) {
    tap_x = lx; tap_y = ly; cap = -1;
    for (int pass = 0; pass < 1; pass++) {
        int i = hit_find(lx, ly); if (i < 0) return;
        hit_t *h = &hits[i];
        switch (h->id) {
        case H_TITLE: audio_play(A_DECIDE); ui_enter(UI_MENU); break;
        case H_PLAY: audio_play(A_DECIDE); ui_enter(UI_STAGES); break;
        case H_RECORD: audio_play(A_DECIDE); rec_page = sel_stage; ui_enter(UI_RECORDS); break;
        case H_OPTION: audio_play(A_DECIDE); opt_back = UI_MENU; ui_enter(UI_OPTION); break;
        case H_HELP: audio_play(A_DECIDE); help_back = UI_MENU; help_page = 0; ui_enter(UI_HELP); break;
        case H_ACH: audio_play(A_DECIDE); ach_page = 0; ui_enter(UI_ACH); break;
        case H_P_HELP: audio_play(A_DECIDE); help_back = UI_PLAY; help_page = 0; ui_enter(UI_HELP); break;
        case H_ORIENT: audio_play(A_SELECT); SV.orient = (SV.orient + 1) % 3; orient_dirty = 1; save_write(); break;
        case H_KEYS: audio_play(A_DECIDE); ui_enter(UI_KEYS); break;
        case H_ABSWAP: { int t = SV.pad[ACT_OK]; SV.pad[ACT_OK] = SV.pad[ACT_BACK]; SV.pad[ACT_BACK] = t; audio_play(A_SELECT); save_write(); break; }       /* A and B trade jobs (3DS) */
        case H_O_SND: audio_play(A_DECIDE); ui_enter(UI_OPT_SOUND); break;
        case H_O_VID: audio_play(A_DECIDE); ui_enter(UI_OPT_VIDEO); break;
        case H_O_CTL: audio_play(A_DECIDE); ui_enter(UI_OPT_CTRL); break;
        case H_FS: audio_play(A_SELECT); fs_req = 1; break;
        case H_RESET: audio_play(A_CANCEL); ctl_defaults(&SV); save_write(); break;
        case H_SCALE: audio_play(A_SELECT); SV.intscale = !SV.intscale; save_write(); break;
        case H_HDR: audio_play(A_SELECT); SV.hdr = !SV.hdr; save_write(); break;
        case H_TOUCHUI: SV.touchui = (SV.touchui + 1) % 3; audio_play(A_SELECT); save_write(); break;
        case H_MATCH: audio_play(A_DECIDE); ctl_match_screen(&SV, SV.orient); save_write(); break;
        case H_BACK:
            audio_play(A_CANCEL);
            if (ui_mode == UI_KEYS) { save_write(); ui_enter(UI_OPT_CTRL); }
            else if (ui_mode == UI_OPT_SOUND || ui_mode == UI_OPT_VIDEO || ui_mode == UI_OPT_CTRL) { save_write(); ui_enter(UI_OPTION); }
            else if (ui_mode == UI_OPTION && opt_back == UI_PLAY) { ui_mode = UI_PLAY; save_write(); }
            else if (ui_mode == UI_HELP && help_back == UI_PLAY) ui_mode = UI_PLAY;
            else { if (ui_mode == UI_OPTION) save_write(); ui_enter(ui_mode == UI_STAGES || ui_mode == UI_RECORDS || ui_mode == UI_OPTION || ui_mode == UI_HELP || ui_mode == UI_ACH ? UI_MENU : UI_TITLE); }
            break;
        case H_PREV: case H_NEXT: {
            int d = h->id == H_PREV ? 7 : 1; audio_play(A_SELECT);
            if (ui_mode == UI_RECORDS) rec_page = (rec_page + d) % 8;
            else if (ui_mode == UI_HELP) help_page = (help_page + (h->id == H_PREV ? 8 : 1)) % 9;
            else if (ui_mode == UI_ACH) ach_page = (ach_page + (h->id == H_PREV ? 8 : 1)) % 9;
            else { sel_stage = (sel_stage + d) % 8; pnj_new(&G, 1, sel_stage); }
            break; }
        case H_START: if (stage_ready(sel_stage)) { audio_play(A_DECIDE); start_stage(sel_stage); } else audio_play(A_CANCEL); break;
        case H_SFXT: SV.sfx_on = !SV.sfx_on; save_apply(); audio_play(A_SELECT); break;
        case H_MUST: SV.mus_on = !SV.mus_on; save_apply(); audio_play(A_SELECT); break;
        case H_SFXS: case H_MUSS: drag_id = h->id; drag_x = h->x + 8; drag_w = h->w - 16; apply_slider(h->id, lx); break;
        case H_CTRL: SV.analog = !SV.analog; save_apply(); IN.active = 0; touch_dir = 0; audio_play(A_SELECT); break;
        case H_SNAP: SV.snappy = !SV.snappy; save_apply(); audio_play(A_SELECT); break;
        case H_P_OPTION: audio_play(A_DECIDE); opt_back = UI_PLAY; ui_enter(UI_OPTION); break;
        case H_P_EXIT: audio_play(A_CANCEL); paused = 0; ui_enter(UI_STAGES); break;
        case H_P_RESUME: audio_play(A_DECIDE); paused = 0; break;
        case H_QUIT: case H_P_QUIT: { SDL_Event qe; SDL_zero(qe); qe.type = SDL_EVENT_QUIT; SDL_PushEvent(&qe); break; }
        case H_OVER: audio_play(A_DECIDE); ui_enter(UI_STAGES); break;
        default:
            if (h->id >= H_KEY0 && h->id < H_KEY0 + ACT_N * 3) { audio_play(A_SELECT); cap = h->id - H_KEY0; }
            break;
        }
        return;
    }
}
static void ui_drag(double lx) { if (drag_id != H_NONE) apply_slider(drag_id, lx); }

/* ---------- keyboard / gamepad menu navigation ----------
 * Arrows move the focus to the nearest control in that direction, Enter/Space (pad: A) presses it, Esc (pad: B) goes back.
 * On the paged screens (stages, records, help, achievements) Left/Right turn the page. On a sound row Left/Right moves its slider. */
static int kb_active(void) { return ui_mode != UI_PLAY || paused; }
static int kb_ok(int i) { const hit_t *h = &hits[i]; return h->id != H_TITLE && h->id != H_OVER && h->id != H_SFXS && h->id != H_MUSS &&   /* a slider is driven from its on/off button */ (long)h->w * h->h < (long)PNJ_W * PNJ_H / 2; }
static int kb_default(void) {
    static const int pref[] = { H_PLAY, H_START, H_P_RESUME };
    for (int k = 0; k < 3; k++) for (int i = 0; i < nhits; i++) if (hits[i].id == pref[k]) return i;
    for (int i = 0; i < nhits; i++) if (kb_ok(i) && hits[i].id != H_BACK) return i;
    for (int i = 0; i < nhits; i++) if (kb_ok(i)) return i;
    return -1;
}
static void kb_sync(void) {                          /* a new screen starts with no focus */
    int sig = ui_mode * 2 + (paused ? 1 : 0);
    if (sig != kb_sig) { kb_sig = sig; kb_cur = -1; }
    if (kb_cur >= nhits || (kb_cur >= 0 && !kb_ok(kb_cur))) kb_cur = -1;
}
static void kb_press(int i) {
    int sig = ui_mode * 2 + (paused ? 1 : 0);
    ui_touch(hits[i].x + hits[i].w / 2.0, hits[i].y + hits[i].h / 2.0);
    if (ui_mode * 2 + (paused ? 1 : 0) != sig) { kb_cur = -1; kb_sig = ui_mode * 2 + (paused ? 1 : 0); }     /* the press opened another screen */
}
static void kb_nav(int dx, int dy) {
    kb_sync(); kb_on = 1;
    if (kb_paged()) {                                        /* paged screens have no focus: Left/Right turn the page */
        if (dx) for (int i = 0; i < nhits; i++) if (hits[i].id == (dx < 0 ? H_PREV : H_NEXT)) { kb_press(i); return; }
        return;
    }
    if (kb_cur < 0) {
        int lit = ui_mode == UI_MENU || (ui_mode == UI_PLAY && paused);       /* these already show a lit default, so the first key moves off it */
        kb_cur = kb_default(); if (!lit) return;
    }
    const hit_t *c = &hits[kb_cur];
    if (dx && (c->id == H_SFXT || c->id == H_MUST)) {                      /* Left/Right on a sound row moves its slider */
        int sid = c->id == H_SFXT ? H_SFXS : H_MUSS;
        for (int i = 0; i < nhits; i++) if (hits[i].id == sid) {
            double v = sid == H_SFXS ? SV.sfx_vol : SV.mus_vol; v += dx * 0.05; v = v < 0 ? 0 : v > 1 ? 1 : v;
            drag_x = hits[i].x + 8; drag_w = hits[i].w - 16; apply_slider(sid, drag_x + drag_w * v); audio_play(A_SELECT); return;
        }
    }
    if (dx && c->id == H_CTRL) { kb_press(kb_cur); return; }                 /* Left/Right on the control selector flips it */
    double cx = c->x + c->w / 2.0, cy = c->y + c->h / 2.0, near = 1e18; int bi = -1;
    for (int pass = 0; pass < 2; pass++) {                   /* pass 0: how far to the next row/column; pass 1: nearest control within it */
        double best = 1e18;
        for (int i = 0; i < nhits; i++) {
            if (i == kb_cur || !kb_ok(i)) continue;
            double ex = hits[i].x + hits[i].w / 2.0 - cx, ey = hits[i].y + hits[i].h / 2.0 - cy;
            double along = dx ? ex * dx : ey * dy, across = fabs(dx ? ey : ex);
            if (along < 4) continue;
            if (pass == 0) { if (along < near) near = along; }
            else if (along <= near + 14 && across < best) { best = across; bi = i; }
        }
    }
    if (bi >= 0) { kb_cur = bi; audio_play(A_SELECT); }
}
static void kb_activate(void) {
    kb_sync(); kb_on = 1;
    if (kb_paged()) {                                        /* stage select: start the stage; the other pages: next page */
        int want = ui_mode == UI_STAGES ? H_START : H_NEXT;
        for (int i = 0; i < nhits; i++) if (hits[i].id == want) { kb_press(i); return; }
        return;
    }
    if (kb_cur >= 0) { kb_press(kb_cur); return; }
    int d = kb_default();
    if (d >= 0 && (ui_mode == UI_MENU || (ui_mode == UI_PLAY && paused))) kb_press(d);
    else if (d >= 0) kb_cur = d;                             /* options: Enter lights the first row before it changes anything */
    else ui_touch(PNJ_W / 2.0, PNJ_H / 2.0);                 /* title and game over: the whole screen is the button */
}
static void kb_back(void) {
    kb_on = 1;
    if (ui_mode == UI_PLAY) { paused = !paused; return; }
    if (ui_mode == UI_TITLE || ui_mode == UI_OVER) { if (ui_mode == UI_OVER) ui_touch(PNJ_W / 2.0, PNJ_H / 2.0); return; }
    for (int i = 0; i < nhits; i++) if (hits[i].id == H_BACK) { kb_press(i); return; }
}

/* called when a run ends: records, total, sounds */
static void run_finished(void) {
    int h = pnj_height(&G), *t = SV.top[cur_stage];
    int tops0[8], total0 = SV.total; for (int k = 0; k < 8; k++) tops0[k] = SV.top[k][0];
    over_score = h; over_new = h > t[0];
    if (h > 0 && h > t[4]) { t[4] = h; for (int i = 4; i > 0 && t[i] > t[i - 1]; i--) { int x = t[i]; t[i] = t[i - 1]; t[i - 1] = x; } }
    SV.total = SV.total + h > 999999999 ? 999999999 : SV.total + h;
    over_nach = 0;
    for (int i = 0; i < 90; i++) if (!ach_done_with(i, tops0, total0) && ach_done(i)) over_ach[over_nach++] = i;
    save_write(); save_commit();
    audio_play(over_new ? A_HISCORE : A_GAMEOVER);
    sel_stage = cur_stage; ui_enter(UI_OVER);
}

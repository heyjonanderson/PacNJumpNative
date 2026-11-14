/* Nintendo DS drawing layer for the SDL shell (platform/sdl/main.c). Same idea as platform/dc/dc_gfx.h: the shell keeps calling
 * SDL_RenderTexture / SDL_RenderFillRect and these turn them into quads for the DS's 3D hardware.
 *
 * Screens. The 320x480 field is drawn at 0.8, which is 256x384: exactly both screens stacked. The top screen shows field rows 0..239 and the touch
 * screen rows 240..479. The 3D hardware can only feed one screen at a time, so the two take turns: each frame one screen gets new 3D output while
 * the other shows a captured copy of its last picture (the usual DS two-screen 3D arrangement). Each screen is therefore redrawn 30 times a second.
 *
 * Textures. The shell asks for "sheet N, this source rectangle". The DS never has the sheets: it has one page per stage (nds/stage<n>.ndp in the
 * pack, built by tools/make_ndspack.py) that holds every rectangle the game draws while that stage is selected, already at 0.8 so one texel is one
 * screen pixel. A rectangle is looked up by (sheet, x, y, w, h); one that is not in the page but lies inside one that is (part of a sprite) is drawn
 * from that one. Each cell is one or more tiles with their own 16-colour palette.
 *
 * What the DS cannot do: no additive blending (glow sprites are drawn as ordinary see-through sprites), 5 bits of transparency, one transparent
 * colour per texture (no soft edges), and about 1700 quads a frame.
 *
 * Included by nds_sdl.h, which has already defined SDL_FRect and the blend / scale mode constants. PNJ_NDS_HOST swaps the hardware for a software
 * rasteriser so the same code can be run and looked at on a PC. */
#ifndef PNJ_NDS_GFX_H
#define PNJ_NDS_GFX_H

#define NDS_SC 0.8f
#define NDS_PAGE_W 1024
#define NDS_PAGE_H 512
#define NDS_MAXQ 1700

typedef struct pnj_tex { int id, w, h; int add, alpha, cr, cg, cb; } pnj_tex;
typedef struct pnj_win { int unused; } pnj_win;
typedef struct { int cr, cg, cb, ca; int draw_add; float ox, oy, sc; int screen; } pnj_gfx;
#define SDL_Renderer pnj_gfx
#define SDL_Texture pnj_tex
#define SDL_Window pnj_win

static pnj_gfx pnj_gfx_one;
static pnj_tex nds_texs[32];
static pnj_tex *nds_tex(int i) { pnj_tex *t = &nds_texs[i & 31]; if (!t->w) { t->id = i; t->w = t->h = i == 0 ? 256 : 512; t->alpha = 255; t->cr = t->cg = t->cb = 255; } return t; }

/* ---------- the page ---------- */
typedef struct { uint8_t tex, pad; int16_t x, y, w, h; uint16_t dw, dh, tile0, ntile; } nd_cell;      /* 18 bytes, as in the file */
typedef struct { uint16_t px, py, w, h, ox, oy, pal, pad; } nd_tile;                                    /* 16 bytes */
static struct { int stage; int ncell, ntile, npal; nd_cell *cell; nd_tile *tile; unsigned char *blob; int16_t hash[1024]; } NP = { -1, 0, 0, 0, NULL, NULL, NULL, { 0 } };
static unsigned nds_misses;                                   /* rectangles asked for and not in the page (nothing is drawn for them) */
static unsigned nd_hash(int tex, int x, int y, int w, int h) { unsigned v = (unsigned)tex * 73856093u ^ (unsigned)x * 19349663u ^ (unsigned)y * 83492791u ^ (unsigned)w * 2971215073u ^ (unsigned)h * 433494437u; return (v ^ (v >> 13)) & 1023; }

/* the back end: where the page's pixels go and how one quad is drawn */
static void nq_upload(const unsigned char *pix, const uint16_t *pal, int npal);
static void nq_quad(const float *p, int textured, float u0, float v0, float u1, float v1, int pal, int r, int g, int b, int a);

static unsigned char *pack_read(const char *name, size_t *size);        /* pack.h */
/* Loads the page for a stage (0-based) if it is not the one in video memory. Returns 1 when a page is ready. */
static int nds_page_set(int stage) {
    if (stage == NP.stage) return NP.cell != NULL;
    char nm[32]; size_t n = 0; snprintf(nm, sizeof nm, "nds/stage%d.ndp", stage + 1);
    unsigned char *d = pack_read(nm, &n);
    NP.stage = stage; free(NP.blob); NP.blob = NULL; NP.cell = NULL; NP.tile = NULL; NP.ncell = NP.ntile = NP.npal = 0;
    if (!d || n < 20 || memcmp(d, "NDP2", 4)) { free(d); return 0; }
    int w = d[4] | (d[5] << 8), h = d[6] | (d[7] << 8);
    uint32_t nc, nt, np; memcpy(&nc, d + 8, 4); memcpy(&nt, d + 12, 4); memcpy(&np, d + 16, 4);
    size_t head = 20 + (size_t)nc * sizeof(nd_cell) + (size_t)nt * sizeof(nd_tile), need = head + (size_t)np * 32 + (size_t)w * h / 2;
    if (w != NDS_PAGE_W || h != NDS_PAGE_H || n < need || nc > 4000 || nt > 8000 || np > 2048) { free(d); return 0; }
    nq_upload(d + head + (size_t)np * 32, (const uint16_t *)(d + head), (int)np);
    unsigned char *keep = (unsigned char *)realloc(d, head); if (!keep) keep = d;           /* the pixels are in video memory now; keep only the tables */
    NP.blob = keep; NP.cell = (nd_cell *)(keep + 20); NP.tile = (nd_tile *)(keep + 20 + (size_t)nc * sizeof(nd_cell)); NP.ncell = (int)nc; NP.ntile = (int)nt; NP.npal = (int)np;
    memset(NP.hash, 0xff, sizeof NP.hash);
    for (int i = 0; i < NP.ncell; i++) {
        const nd_cell *c = &NP.cell[i]; unsigned k = nd_hash(c->tex, c->x, c->y, c->w, c->h);
        for (int tries = 0; tries < 1024; tries++, k = (k + 1) & 1023) if (NP.hash[k] < 0) { NP.hash[k] = (int16_t)i; break; }
    }
    return 1;
}
/* the cell for a source rectangle, and where the rectangle sits inside it (0,0 for an exact match) */
static const nd_cell *nd_find(int tex, int x, int y, int w, int h, int *sx, int *sy) {
    if (!NP.cell) return NULL;
    unsigned k = nd_hash(tex, x, y, w, h);
    for (int tries = 0; tries < 1024; tries++, k = (k + 1) & 1023) {
        int i = NP.hash[k]; if (i < 0) break;
        const nd_cell *c = &NP.cell[i];
        if (c->tex == tex && c->x == x && c->y == y && c->w == w && c->h == h) { *sx = *sy = 0; return c; }
    }
    for (int i = 0; i < NP.ncell; i++) {
        const nd_cell *c = &NP.cell[i];
        if (c->tex == tex && x >= c->x && y >= c->y && x + w <= c->x + c->w && y + h <= c->y + c->h) { *sx = x - c->x; *sy = y - c->y; return c; }
    }
    return NULL;
}

/* ---------- SDL drawing calls the shell makes ---------- */
static inline int pnj_f2b(float v) { return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255.0f + 0.5f); }
static void pnj_set_color(pnj_gfx *g, int r, int gr, int b, int a) { g->cr = r & 255; g->cg = gr & 255; g->cb = b & 255; g->ca = a & 255; }
#define SDL_SetRenderDrawColor(r, R, G, B, A) pnj_set_color((r), (R), (G), (B), (A))
#define SDL_SetRenderDrawColorFloat(r, R, G, B, A) pnj_set_color((r), pnj_f2b(R), pnj_f2b(G), pnj_f2b(B), pnj_f2b(A))
#define SDL_SetRenderDrawBlendMode(r, m) ((r)->draw_add = ((m) == SDL_BLENDMODE_ADD))
static void nq_clear(int r, int g, int b);
#define SDL_RenderClear(r) nq_clear((r)->cr, (r)->cg, (r)->cb)
#define PNJ_DEPTH(r, z) ((void)0)
static void pnj_fill(pnj_gfx *g, const SDL_FRect *q) {
    if (!g->ca) return;
    float x0 = q->x * g->sc + g->ox, y0 = q->y * g->sc + g->oy, x1 = x0 + q->w * g->sc, y1 = y0 + q->h * g->sc;
    if (y1 <= 0 || y0 >= 192 || x1 <= 0 || x0 >= 256) return;
    float p[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
    nq_quad(p, 0, 0, 0, 0, 0, 0, g->cr, g->cg, g->cb, g->ca);
}
#define SDL_RenderFillRect(r, q) pnj_fill((r), (q))
static void pnj_rect_outline(pnj_gfx *g, const SDL_FRect *q) {
    SDL_FRect a = { q->x, q->y, q->w, 1.25f }, b = { q->x, q->y + q->h - 1.25f, q->w, 1.25f }, c = { q->x, q->y, 1.25f, q->h }, d = { q->x + q->w - 1.25f, q->y, 1.25f, q->h };
    pnj_fill(g, &a); pnj_fill(g, &b); pnj_fill(g, &c); pnj_fill(g, &d);
}
#define SDL_RenderRect(r, q) pnj_rect_outline((r), (q))
static void pnj_line(pnj_gfx *g, float x0, float y0, float x1, float y1) {      /* a one pixel wide quad along the line */
    if (!g->ca) return;
    x0 = x0 * g->sc + g->ox; y0 = y0 * g->sc + g->oy; x1 = x1 * g->sc + g->ox; y1 = y1 * g->sc + g->oy;
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 0.001f) return;
    float nx = -dy / l * 0.5f, ny = dx / l * 0.5f;
    float p[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    nq_quad(p, 0, 0, 0, 0, 0, 0, g->cr, g->cg, g->cb, g->ca);
}
#define SDL_RenderLine(r, a, b, c, d) pnj_line((r), (a), (b), (c), (d))

#define SDL_DestroyTexture(t) ((void)(t))
#define SDL_SetTextureBlendMode(t, m) ((t)->add = ((m) == SDL_BLENDMODE_ADD))
#define SDL_SetTextureAlphaMod(t, a) ((t)->alpha = (a))
#define SDL_SetTextureColorMod(t, R, G, B) ((t)->cr = (R), (t)->cg = (G), (t)->cb = (B))
#define SDL_SetTextureColorModFloat(t, R, G, B) ((t)->cr = pnj_f2b(R), (t)->cg = pnj_f2b(G), (t)->cb = pnj_f2b(B))
#define SDL_SetTextureScaleMode(t, m) ((void)0)

static void pnj_draw_tex(pnj_gfx *g, pnj_tex *t, const SDL_FRect *src, const SDL_FRect *dst, double angle) {
    if (!src || !t) return;
    int al = t->alpha < 0 ? 0 : t->alpha > 255 ? 255 : t->alpha; if (al < 5) return;
    float w = dst->w * g->sc, h = dst->h * g->sc, hw = w * 0.5f, hh = h * 0.5f;
    float cx = dst->x * g->sc + g->ox + hw, cy = dst->y * g->sc + g->oy + hh;
    float rad = angle != 0.0 ? (hw > hh ? hw : hh) * 1.5f : 0.0f;
    if (cy + (rad > 0 ? rad : hh) <= 0 || cy - (rad > 0 ? rad : hh) >= 192 || cx + (rad > 0 ? rad : hw) <= 0 || cx - (rad > 0 ? rad : hw) >= 256) return;   /* not on this screen */
    int sw = (int)src->w, sh = (int)src->h, subx, suby;
    const nd_cell *c = nd_find(t->id, (int)src->x, (int)src->y, sw, sh, &subx, &suby);
    if (!c) {
#ifdef PNJ_NDS_HOST
        { static FILE *mf; static int tried; if (!tried) { tried = 1; const char *mp = getenv("PNJ_NDS_MISSLOG"); if (mp) mf = fopen(mp, "a"); }
          if (mf) { fprintf(mf, "%d %d %d %d %d\n", t->id, (int)src->x, (int)src->y, sw, sh); fflush(mf); } }
#endif
        nds_misses++; return;
    }
    if (sw < 1 || sh < 1) return;
    float kx = (float)c->dw / (float)c->w, ky = (float)c->dh / (float)c->h;            /* texels per sheet unit */
    float rx0 = subx * kx, ry0 = suby * ky, rw = sw * kx, rh = sh * ky;                /* the asked-for rectangle, in the cell's texels */
    float px = w / rw, py = h / rh;                                                    /* screen pixels per texel */
    float co = 1.0f, si = 0.0f;
    if (angle != 0.0) { float a = (float)(angle * 3.14159265358979 / 180.0); co = cosf(a); si = sinf(a); }
    int rr = t->cr, gg = t->cg, bb = t->cb;
    const nd_tile *tl = &NP.tile[c->tile0];
    for (int i = 0; i < c->ntile; i++, tl++) {
        if (!tl->w) continue;
        float ax0 = tl->ox, ay0 = tl->oy, ax1 = ax0 + tl->w, ay1 = ay0 + tl->h;        /* the tile inside the cell, clipped to the rectangle */
        if (ax0 < rx0) ax0 = rx0;
        if (ay0 < ry0) ay0 = ry0;
        if (ax1 > rx0 + rw) ax1 = rx0 + rw;
        if (ay1 > ry0 + rh) ay1 = ry0 + rh;
        if (ax1 <= ax0 || ay1 <= ay0) continue;
        float lx0 = (ax0 - rx0) * px - hw, ly0 = (ay0 - ry0) * py - hh, lx1 = (ax1 - rx0) * px - hw, ly1 = (ay1 - ry0) * py - hh;
        float p[8];
        if (angle != 0.0) {
            p[0] = cx + lx0 * co - ly0 * si; p[1] = cy + lx0 * si + ly0 * co; p[2] = cx + lx1 * co - ly0 * si; p[3] = cy + lx1 * si + ly0 * co;
            p[4] = cx + lx1 * co - ly1 * si; p[5] = cy + lx1 * si + ly1 * co; p[6] = cx + lx0 * co - ly1 * si; p[7] = cy + lx0 * si + ly1 * co;
        } else {
            if (cy + ly1 <= 0 || cy + ly0 >= 192) continue;
            p[0] = cx + lx0; p[1] = cy + ly0; p[2] = cx + lx1; p[3] = cy + ly0; p[4] = cx + lx1; p[5] = cy + ly1; p[6] = cx + lx0; p[7] = cy + ly1;
        }
        nq_quad(p, 1, tl->px + (ax0 - tl->ox), tl->py + (ay0 - tl->oy), tl->px + (ax1 - tl->ox), tl->py + (ay1 - tl->oy), tl->pal, rr, gg, bb, al);
    }
}
#define SDL_RenderTextureRotated(r, t, s, d, ang, c, f) pnj_draw_tex((r), (t), (s), (d), (ang))
#define SDL_RenderTexture(r, t, s, d) pnj_draw_tex((r), (t), (s), (d), 0.0)

/* ---------- window and renderer calls that mean nothing here ---------- */
static pnj_win pnj_win_one;
#define SDL_CreateWindow(...) (&pnj_win_one)
#define SDL_DestroyWindow(w) ((void)(w))
#define SDL_DestroyRenderer(r) ((void)(r))
#define SDL_GetWindowFlags(w) ((SDL_WindowFlags)0)
#define SDL_SetWindowFullscreen(w, f) ((void)0)
#define SDL_ShowCursor() ((void)0)
#define SDL_HideCursor() ((void)0)
#define SDL_SetRenderLogicalPresentation(r, w, h, m) ((void)0)

#ifndef PNJ_NDS_HOST
/* ================= the DS itself ================= */
static int nq_n, nq_pid;
static void nq_upload(const unsigned char *pix, const uint16_t *pal, int npal) {
    swiWaitForVBlank();
    vramSetBankA(VRAM_A_LCD); vramSetBankB(VRAM_B_LCD);
    memcpy((void *)VRAM_A, pix, 128 * 1024); memcpy((void *)VRAM_B, pix + 128 * 1024, 128 * 1024);
    vramSetBankA(VRAM_A_TEXTURE_SLOT0); vramSetBankB(VRAM_B_TEXTURE_SLOT1);
    vramSetBankE(VRAM_E_LCD);
    memcpy((void *)VRAM_E, pal, (size_t)npal * 32);
    vramSetBankE(VRAM_E_TEX_PALETTE);
}
static void nq_clear(int r, int g, int b) { glClearColor(r >> 3, g >> 3, b >> 3, 31); }
/* One quad. Corners come in screen pixels, clockwise from the top left; the projection works in eighths of a pixel. Every quad sits a little
 * nearer than the one before, so later ones cover earlier ones. See-through quads each take a new polygon id, or they would not blend over each other. */
static void nq_quad(const float *p, int textured, float u0, float v0, float u1, float v1, int pal, int r, int g, int b, int a) {
    int a5 = (a * 31 + 127) / 255; if (a5 < 1 || nq_n >= NDS_MAXQ) return;
    nq_n++;
    glPolyFmt(POLY_ALPHA(a5) | POLY_CULL_NONE | POLY_ID(a5 == 31 ? 0 : 1 + (nq_pid++ % 60)));
    if (textured) {
        GFX_TEX_FORMAT = (7u << 20) | (6u << 23) | (3u << 26) | (1u << 29);        /* 1024 x 512, 16 colours, colour 0 transparent, at the start of texture memory */
        GFX_PAL_FORMAT = (u32)pal * 2;                                           /* palettes are 32 bytes; the register counts in 16s */
    } else GFX_TEX_FORMAT = 0;
    glColor3b((u8)r, (u8)g, (u8)b);
    int z = -3600 + nq_n * 2;
    t16 s0 = (t16)(u0 * 16.0f), t0 = (t16)(v0 * 16.0f), s1 = (t16)(u1 * 16.0f), t1 = (t16)(v1 * 16.0f);
    glBegin(GL_QUADS);
    glTexCoord2t16(s0, t0); glVertex3v16((v16)(p[0] * 8.0f), (v16)(p[1] * 8.0f), (v16)z);
    glTexCoord2t16(s1, t0); glVertex3v16((v16)(p[2] * 8.0f), (v16)(p[3] * 8.0f), (v16)z);
    glTexCoord2t16(s1, t1); glVertex3v16((v16)(p[4] * 8.0f), (v16)(p[5] * 8.0f), (v16)z);
    glTexCoord2t16(s0, t1); glVertex3v16((v16)(p[6] * 8.0f), (v16)(p[7] * 8.0f), (v16)z);
}
static pnj_gfx *pnj_gfx_open(void) {
    pnj_gfx *g = &pnj_gfx_one;
    videoSetMode(MODE_0_3D);
    videoSetModeSub(MODE_5_2D);
    glInit();
    /* The lower engine shows the captured picture of whichever screen the 3D is not on: as twelve 64x64 bitmap sprites when the capture is in bank D,
       as a bitmap background when it is in bank C. */
    oamInit(&oamSub, SpriteMapping_Bmp_2D_256, false);
    { int id = 0;
      for (int y = 0; y < 3; y++) for (int x = 0; x < 4; x++) {
          oamSub.oamMemory[id].attribute[0] = ATTR0_BMP | ATTR0_SQUARE | (64 * y);
          oamSub.oamMemory[id].attribute[1] = ATTR1_SIZE_64 | (64 * x);
          oamSub.oamMemory[id].attribute[2] = ATTR2_ALPHA(1) | (8 * 32 * y) | (8 * x);
          id++;
      } }
    swiWaitForVBlank(); oamUpdate(&oamSub);
    bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND); glDisable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31); glClearPolyID(63); glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    vramSetBankA(VRAM_A_TEXTURE_SLOT0); vramSetBankB(VRAM_B_TEXTURE_SLOT1); vramSetBankE(VRAM_E_TEX_PALETTE);
    g->sc = NDS_SC; g->ca = 255;
    return g;
}
/* Starts a frame and says which screen it is for (0 top, 1 touch). What is sent this frame is shown on the NEXT one, so while the 3D output is on the
 * lower screen (showing what was sent last time) the top screen's quads are sent, and the other way round. */
static int pnj_gfx_begin(pnj_gfx *g) {
    static int phase;
    while (REG_DISPCAPCNT & DCAP_ENABLE) ;
    phase ^= 1;
    if (phase) { lcdMainOnBottom(); vramSetBankC(VRAM_C_LCD); vramSetBankD(VRAM_D_SUB_SPRITE); REG_DISPCAPCNT = DCAP_BANK(2) | DCAP_ENABLE | DCAP_SIZE(3); g->screen = 0; }
    else       { lcdMainOnTop();    vramSetBankD(VRAM_D_LCD); vramSetBankC(VRAM_C_SUB_BG);     REG_DISPCAPCNT = DCAP_BANK(3) | DCAP_ENABLE | DCAP_SIZE(3); g->screen = 1; }
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrthof32(0, 256 * 8, 192 * 8, 0, -4096, 4096);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    nq_n = 0; nq_pid = 0;
    g->sc = NDS_SC; g->ox = 0.0f; g->oy = g->screen ? -192.0f : 0.0f;
    return g->screen;
}
static void pnj_gfx_end(pnj_gfx *g) { (void)g; glFlush(GL_TRANS_MANUALSORT); swiWaitForVBlank(); }

#else
/* ================= PC test build: a software rasteriser with the DS's rules ================= */
static unsigned char *hq_pix; static uint16_t *hq_pal; static int hq_npal;
static unsigned char hq_fb[2][192][256][3]; static int hq_screen, hq_n;
static void nq_upload(const unsigned char *pix, const uint16_t *pal, int npal) {
    free(hq_pix); free(hq_pal);
    hq_pix = (unsigned char *)malloc(NDS_PAGE_W * NDS_PAGE_H / 2); memcpy(hq_pix, pix, NDS_PAGE_W * NDS_PAGE_H / 2);
    hq_pal = (uint16_t *)malloc((size_t)npal * 32); memcpy(hq_pal, pal, (size_t)npal * 32); hq_npal = npal;
}
static void nq_clear(int r, int g, int b) { for (int y = 0; y < 192; y++) for (int x = 0; x < 256; x++) { hq_fb[hq_screen][y][x][0] = (unsigned char)(r & 0xf8); hq_fb[hq_screen][y][x][1] = (unsigned char)(g & 0xf8); hq_fb[hq_screen][y][x][2] = (unsigned char)(b & 0xf8); } }
static void nq_quad(const float *p, int textured, float u0, float v0, float u1, float v1, int pal, int r, int g, int b, int a) {
    int a5 = (a * 31 + 127) / 255; if (a5 < 1 || hq_n >= NDS_MAXQ) return;
    hq_n++;
    float ex = p[2] - p[0], ey = p[3] - p[1], fx = p[6] - p[0], fy = p[7] - p[1], det = ex * fy - ey * fx;
    if (det > -1e-6f && det < 1e-6f) return;
    float minx = p[0], maxx = p[0], miny = p[1], maxy = p[1];
    for (int i = 2; i < 8; i += 2) { if (p[i] < minx) minx = p[i]; if (p[i] > maxx) maxx = p[i]; if (p[i + 1] < miny) miny = p[i + 1]; if (p[i + 1] > maxy) maxy = p[i + 1]; }
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 256) x1 = 256;
    if (y1 > 192) y1 = 192;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = x + 0.5f - p[0], dy = y + 0.5f - p[1];
        float s = (dx * fy - dy * fx) / det, t = (ex * dy - ey * dx) / det;
        if (s < 0 || s >= 1 || t < 0 || t >= 1) continue;
        int cr = r, cg = g, cb = b;
        if (textured) {
            int tx = (int)(u0 + s * (u1 - u0)), ty = (int)(v0 + t * (v1 - v0));
            if (tx < 0 || ty < 0 || tx >= NDS_PAGE_W || ty >= NDS_PAGE_H || !hq_pix) continue;
            int byte = hq_pix[(ty * NDS_PAGE_W + tx) / 2], ix = (tx & 1) ? byte >> 4 : byte & 15;
            if (!ix || pal >= hq_npal) continue;
            int c = hq_pal[pal * 16 + ix];
            cr = (((c & 31) << 3) * r) / 255; cg = ((((c >> 5) & 31) << 3) * g) / 255; cb = ((((c >> 10) & 31) << 3) * b) / 255;
        }
        unsigned char *o = hq_fb[hq_screen][y][x];
        o[0] = (unsigned char)(((cr * a5 + o[0] * (31 - a5)) / 31) & 0xf8); o[1] = (unsigned char)(((cg * a5 + o[1] * (31 - a5)) / 31) & 0xf8); o[2] = (unsigned char)(((cb * a5 + o[2] * (31 - a5)) / 31) & 0xf8);
    }
}
static pnj_gfx *pnj_gfx_open(void) { pnj_gfx_one.sc = NDS_SC; pnj_gfx_one.ca = 255; return &pnj_gfx_one; }
static int pnj_gfx_begin(pnj_gfx *g) {
    static int phase; phase ^= 1; g->screen = phase ? 0 : 1; hq_screen = g->screen; hq_n = 0;
    g->sc = NDS_SC; g->ox = 0.0f; g->oy = g->screen ? -192.0f : 0.0f;
    return g->screen;
}
/* PNJ_NDS_SHOT="frame:file.ppm,frame:file.ppm": writes both screens (256x384, top over bottom) when that frame has been drawn; the run ends after the last one */
static void pnj_gfx_end(pnj_gfx *g) {
    (void)g; host_frames++;
    static const char *s; static int init, nextf = -1; static char file[256];
    if (!init) { init = 1; s = getenv("PNJ_NDS_SHOT"); }
    if (nextf < 0 && s && *s) {
        nextf = (int)strtol(s, (char **)&s, 10); int k = 0;
        if (*s == ':') s++;
        while (*s && *s != ',' && k < 255) file[k++] = *s++;
        file[k] = 0; if (*s == ',') s++;
    }
    if (nextf >= 0 && host_frames >= (Uint64)nextf) {
        FILE *f = fopen(file, "wb");
        if (f) { fprintf(f, "P6\n256 384\n255\n"); fwrite(hq_fb, 1, sizeof hq_fb, f); fclose(f); }
        fprintf(stderr, "shot %s at frame %d, quads %d, misses %u\n", file, nextf, hq_n, nds_misses);
        nextf = -1;
        if (!s || !*s) exit(0);
    }
    if (host_frames > 200000) exit(0);
}
#endif

#endif

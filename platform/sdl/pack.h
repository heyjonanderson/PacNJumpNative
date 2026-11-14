/* Reader for pnj.pack (built by tools/make_pack.py from the user's own APK). Included by main.c before read_asset.
 * Format (little endian): "PNJPACK1", u32 count, count x { u16 name_len, name, u32 offset, u32 size }, then file data. */
typedef struct { char name[56]; Uint32 off, size; } pk_ent;
static Uint32 io_u32(SDL_IOStream *f) { Uint32 v = 0; SDL_ReadU32LE(f, &v); return v; }
static Uint16 io_u16(SDL_IOStream *f) { Uint16 v = 0; SDL_ReadU16LE(f, &v); return v; }
static pk_ent *PK; static int PKN; static SDL_IOStream *PKF; static char pack_path[512];

static int pack_open(const char *path) {
    SDL_IOStream *f = SDL_IOFromFile(path, "rb"); if (!f) return 0;
    char magic[8]; Uint32 n;
    if (SDL_ReadIO(f, magic, 8) != 8 || memcmp(magic, "PNJPACK1", 8) || (n = io_u32(f)) == 0 || n > 4096) { SDL_CloseIO(f); return 0; }
    pk_ent *e = (pk_ent *)calloc(n, sizeof *e);
    for (Uint32 i = 0; i < n; i++) {
        Uint16 l = io_u16(f); char nm[256];
        if (l >= sizeof nm || SDL_ReadIO(f, nm, l) != l) { free(e); SDL_CloseIO(f); return 0; }
        nm[l] = 0; snprintf(e[i].name, sizeof e[i].name, "%s", nm);
        e[i].off = io_u32(f); e[i].size = io_u32(f);
    }
    if (PKF) SDL_CloseIO(PKF);
    free(PK); PK = e; PKN = (int)n; PKF = f; snprintf(pack_path, sizeof pack_path, "%s", path);
    return 1;
}
static unsigned char *pack_read(const char *name, size_t *size) {
    for (int i = 0; i < PKN; i++) if (!strcmp(PK[i].name, name)) {
        unsigned char *b = (unsigned char *)malloc(PK[i].size ? PK[i].size : 1); if (!b) return 0;
        if (SDL_SeekIO(PKF, PK[i].off, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(PKF, b, PK[i].size) != PK[i].size) { free(b); return 0; }
        *size = PK[i].size; return b;
    }
    return 0;
}
static int pack_has(const char *name) { for (int i = 0; i < PKN; i++) if (!strcmp(PK[i].name, name)) return 1; return 0; }

#if defined(PNJ_N3DS) || defined(PNJ_NDS)
#include <sys/stat.h>
#endif
/* Where the pack and the save file live. Windows and Linux: next to the program when that folder is writable (everything stays in one
   folder, nothing in AppData), otherwise the per-user data folder. macOS, Android, iOS and Vita always use the per-user folder (the app bundle is not a place to write). */
static char data_base[600];
static const char *data_dir(void) {
    static int done; if (done) return data_base; done = 1;
#ifdef PNJ_DC
    snprintf(data_base, sizeof data_base, "/vmu/a1/");               /* settings and records go on the first memory card; the pack is read from the disc or the SD card (pack_find) */
    return data_base;
#endif
#ifdef PNJ_NDS
    if (!data_base[0]) { snprintf(data_base, sizeof data_base, "/pacnjump/"); mkdir("/pacnjump", 0777); }      /* pack_find sets it to wherever the pack was found */
    return data_base;
#endif
#ifdef PNJ_N3DS
    /* fixed folder on the SD card, made here: SDL's pref path was coming back empty on the 3DS */
    snprintf(data_base, sizeof data_base, "sdmc:/3ds/pacnjump/");
    mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/pacnjump", 0777);
    return data_base;
#endif
#if !defined(__ANDROID__) && !defined(__APPLE__) && !defined(__vita__) && !defined(PNJ_N3DS) && !defined(PNJ_DC) && !defined(PNJ_NDS)
    const char *bp = SDL_GetBasePath();
    if (bp) {
        char t[700]; snprintf(t, sizeof t, "%spnj.tmp", bp);
        FILE *f = fopen(t, "wb"); if (f) { fclose(f); remove(t); snprintf(data_base, sizeof data_base, "%s", bp); return data_base; }
    }
#endif
#if !defined(PNJ_DC) && !defined(PNJ_NDS)
    char *pp = SDL_GetPrefPath("jon", "pnj"); if (pp) { snprintf(data_base, sizeof data_base, "%s", pp); SDL_free(pp); }
#endif
    return data_base;
}
static void data_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s%s", data_dir(), name); }

/* find a pack: --pack arg, PNJ_PACK env, the data folder, then the current directory */
static int pack_find(const char *arg) {
#ifdef PNJ_DC
    static const char *const where[] = { "/cd/pnj.pack", "/sd/pnj.pack", "/pc/pnj.pack", "/rd/pnj.pack", NULL };      /* disc, SD card, dc-tool host folder */
    for (int i = 0; where[i]; i++) if (pack_open(where[i])) return 1;
    (void)arg; return 0;
#endif
#ifdef PNJ_NDS
    /* next to the ROM (launchers start a program in its own folder), then a pacnjump folder on the card. Settings and records are saved in the same folder. */
    static const char *const nwhere[] = { "", "/pacnjump/", "/", "fat:/pacnjump/", "sd:/pacnjump/", NULL };
    (void)arg;
    for (int i = 0; nwhere[i]; i++) { char pp[64]; snprintf(pp, sizeof pp, "%spnj_ds.pack", nwhere[i]); if (pack_open(pp)) { snprintf(data_base, sizeof data_base, "%s", nwhere[i]); data_dir(); return 1; } }
    return 0;
#endif
    if (arg && pack_open(arg)) return 1;
    const char *e = getenv("PNJ_PACK"); if (e && pack_open(e)) return 1;
    char p[700]; data_path(p, sizeof p, "pnj.pack"); if (pack_open(p)) return 1;
    return pack_open("pnj.pack");
}

/* the 42 level maps go to the core as a runtime table (names without the .map suffix) */
static pnj_map *PK_MAPS; static int PK_NMAPS;
static void pack_load_maps(void) {
    int n = 0; for (int i = 0; i < PKN; i++) if (!strncmp(PK[i].name, "maps/", 5)) n++;
    if (!n) return;
    PK_MAPS = (pnj_map *)calloc((size_t)n, sizeof *PK_MAPS); PK_NMAPS = 0;
    for (int i = 0; i < PKN; i++) {
        if (strncmp(PK[i].name, "maps/", 5)) continue;
        size_t sz; unsigned char *b = pack_read(PK[i].name, &sz); if (!b) continue;
        char *nm = (char *)malloc(32); snprintf(nm, 32, "%s", PK[i].name + 5);
        char *dot = strrchr(nm, '.'); if (dot) *dot = 0;
        PK_MAPS[PK_NMAPS].name = nm; PK_MAPS[PK_NMAPS].n = (int)sz; PK_MAPS[PK_NMAPS].cells = b; PK_NMAPS++;
    }
    pnj_set_maps(PK_MAPS, PK_NMAPS);
}

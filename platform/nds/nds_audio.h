/* Sound for the Nintendo DS, with the same names the shell uses from platform/sdl/audio.h.
 * Nothing is mixed or decoded here. Every sound in the DS pack (nds/<name>.nda, made by tools/make_ndaudio.py) is already in the DS's own 4-bit
 * ADPCM, and the sound hardware plays it from memory on one of its 16 channels. Music is one looping channel. A track is 400 to 600 KB, so the
 * menu music, the power-up jingle and the current stage's track are simply kept in memory.
 * .nda: "NDA1", u32 rate, u32 samples, u32 data bytes, then the data (a 4-byte ADPCM header and the 4-bit codes).
 * Included by main.c in place of audio.h when PNJ_NDS is defined. */
typedef struct { unsigned char *base, *data; unsigned len, rate; } nda_t;
static nda_t SFXB[32];
static int sfx_on = 1, mus_on = 1;
static float mus_vol = 0.55f, sfx_vol = 0.8f;
static unsigned aud_dev;
static char mus_name[32], mus_want[32];
enum { A_SELECT = 24, A_DECIDE, A_CANCEL, A_GAMEOVER, A_HISCORE, A_EAT_ITEM, A_WARN };

typedef struct { char name[32]; nda_t s; int pin; } nmus_t;
static nmus_t NMC[3]; static int nmus_cur = -1, nmus_ch = -1, nmus_vol = -1, nmus_paused;

static int nda_load(const char *file, nda_t *o) {
    char nm[64]; size_t n = 0; snprintf(nm, sizeof nm, "nds/%s.nda", file);
    unsigned char *d = pack_read(nm, &n);
    if (!d || n < 24 || memcmp(d, "NDA1", 4)) { free(d); return 0; }
    uint32_t rate, len; memcpy(&rate, d + 4, 4); memcpy(&len, d + 12, 4);
    if (len > n - 16 || len < 8 || rate < 4000) { free(d); return 0; }
    o->base = d; o->data = d + 16; o->len = len & ~3u; o->rate = rate; return 1;
}
static void nda_free(nda_t *o) { free(o->base); memset(o, 0, sizeof *o); }
static int nds_vol(float v) { int i = (int)(v * 127.0f + 0.5f); return i < 0 ? 0 : i > 127 ? 127 : i; }
static void nmus_stop(void) {
#ifndef PNJ_NDS_HOST
    if (nmus_ch >= 0) soundKill(nmus_ch);
#endif
    nmus_ch = -1; nmus_cur = -1; mus_name[0] = 0; nmus_paused = 0;
}
static int nmus_pinned(const char *t) { return !strcmp(t, "bgm1") || !strcmp(t, "wakawaka"); }
static int nmus_get(const char *track) {                    /* the slot holding this track, loading it if needed; -1 if it is not in the pack */
    for (int i = 0; i < 3; i++) if (NMC[i].s.base && !strcmp(NMC[i].name, track)) return i;
    int slot = -1;
    for (int i = 0; i < 3; i++) if (!NMC[i].s.base) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < 3; i++) if (!NMC[i].pin && i != nmus_cur) { slot = i; break; }
    if (slot < 0) return -1;
    nda_free(&NMC[slot].s); NMC[slot].name[0] = 0;
    if (!nda_load(track, &NMC[slot].s)) return -1;
    snprintf(NMC[slot].name, sizeof NMC[slot].name, "%s", track); NMC[slot].pin = nmus_pinned(track);
    return slot;
}
static void audio_music_flush(void) { nmus_stop(); for (int i = 0; i < 3; i++) { nda_free(&NMC[i].s); NMC[i].name[0] = 0; } mus_want[0] = 0; }
static void audio_load_all(void) {
    static const char *names[] = { "jump", "break", "mappy_jump", "eat01", "eat02", "eat03", "eat04", "eat05", "eat06", "eat07", "eat08",
        "eat_enemy", "eat_pow", "miss", "bound", "balloon", "candle", "hat" };       /* slots 1.. match PNJ_SFX_* */
    for (int i = 0; i < 32; i++) nda_free(&SFXB[i]);
    for (int i = 0; i < 18; i++) nda_load(names[i], &SFXB[i + 1]);
    static const struct { int slot; const char *f; } more[] = { { A_SELECT, "select" }, { A_DECIDE, "decide" }, { A_CANCEL, "cancel" },
        { A_GAMEOVER, "gameover" }, { A_HISCORE, "hiscore" }, { A_EAT_ITEM, "eat_item" }, { A_WARN, "st8_warning" } };
    for (int i = 0; i < 7; i++) nda_load(more[i].f, &SFXB[more[i].slot]);
    nmus_get("wakawaka"); nmus_get("bgm1");
}
static void audio_init(void) { aud_dev = 1; audio_load_all(); }
static void audio_play(int slot) {
    if (!aud_dev || !sfx_on || slot <= 0 || slot >= 32 || !SFXB[slot].data) return;
#ifndef PNJ_NDS_HOST
    soundPlaySample(SFXB[slot].data, SoundFormat_ADPCM, SFXB[slot].len, (u16)SFXB[slot].rate, (u8)nds_vol(sfx_vol), 64, false, 0);
#endif
}
/* Called every frame with the track that should be playing ("" for none). Also keeps the music volume in step with the options screen. */
static void audio_music(const char *track) {
    if (!aud_dev) return;
    int want_vol = mus_on ? nds_vol(mus_vol) : 0;
    if (strcmp(track, mus_want)) {
        snprintf(mus_want, sizeof mus_want, "%s", track);
        if (!track[0]) nmus_stop();
        else if (strcmp(track, mus_name)) {
            int slot = nmus_get(track);
            if (slot >= 0) {                                  /* a track that is not in the pack leaves the current one playing */
                nmus_stop(); nmus_cur = slot; snprintf(mus_name, sizeof mus_name, "%s", track); nmus_vol = want_vol;
#ifndef PNJ_NDS_HOST
                nmus_ch = soundPlaySample(NMC[slot].s.data, SoundFormat_ADPCM, NMC[slot].s.len, (u16)NMC[slot].s.rate, (u8)want_vol, 64, true, 1);
#endif
            }
        }
    }
    if (nmus_ch >= 0 && want_vol != nmus_vol) {
        nmus_vol = want_vol;
#ifndef PNJ_NDS_HOST
        soundSetVolume(nmus_ch, (u8)want_vol);
#endif
    }
}
static void audio_pause(int p) {
    if (nmus_ch < 0 || p == nmus_paused) return;
    nmus_paused = p;
#ifndef PNJ_NDS_HOST
    if (p) soundPause(nmus_ch); else soundResume(nmus_ch);
#endif
}

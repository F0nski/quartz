/* The global pages TEMPO, SCALE, KEYS, FX, SONG (spec 5g; docs/screens/global.html): one big title and a flat drawing per setting,
 * each legend in its encoder's colour, what plays in pink. Rectangles, lines and text drawn into the band itself:
 * nothing is built ahead. Part of the unity build after scene_sound.c (app_ui.c): sc, PINK3, HM_BLUE .. HM_WHITE,
 * SP_WIRE, SP_BAR. Before ui.c: the numbers are written here. */
#include "scene.h"

#define GL_UI RGB(0x8b, 0x84, 0xa8)         /* the secondary legends */
#define GL_KEY RGB(0xd8, 0xd3, 0xec)        /* the ordinary white keys */
#define GL_BLACK RGB(0x1d, 0x18, 0x38)      /* the ordinary black keys */
#define GL_INK RGB(0xf4, 0xf1, 0xff)
static struct { int pos; uint32_t beat_ms, hits, note_ms; float tb, tn; } gl;   /* the track's step; the last beat; the note-ons seen; the last note; seconds since each */

/* v >= 0 in decimal at b; the end */
static char *glo_num(int v, char *b)
{
    char d[6];
    int n = 0;
    do d[n++] = (char)('0' + v % 10), v /= 10;
    while (v);
    while (n)
        *b++ = d[--n];
    *b = 0;
    return b;
}

static char *glo_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

/* text with its middle on y: left-aligned at x, centred on it (al 0) or right-aligned to it (al 1) */
static void glo_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int al)
{
    cv_text(al == 1 ? x - text_w(f, s) : al == 0 ? x - text_w(f, s) / 2 : x, y - f->h / 2, f, s, c);
}

/* the beat: pos reaching a multiple of 4; the gauges step back and return in 0.25 s */
void glo_frame(const q_glview *v, uint32_t ms)
{
    sc.fresh = !sc.seen;
    sc.seen = 1;
    if (sc.fresh)
        gl.beat_ms = gl.note_ms = ms - 100000u, gl.pos = v->pos, gl.hits = v->hits;
    else if (v->pos != gl.pos) {
        if (v->pos >= 0 && v->pos % 4 == 0)
            gl.beat_ms = ms;
        gl.pos = v->pos;
    }
    if (v->hits != gl.hits)
        gl.note_ms = ms, gl.hits = v->hits;
    gl.tb = (float)(ms - gl.beat_ms) * 0.001f;
    gl.tn = (float)(ms - gl.note_ms) * 0.001f;
}

/* TEMPO: the bpm, a bar of 16 cells (the beats high; beats and 8ths blue, the off-beats green, pushed right by SWING),
   COMP and DUCK as gauges filled from the right */
static void glo_tempo(const q_glview *v, int32_t oy)
{
    char t[16], *e;
    glo_num(v->k[0], t);
    glo_text(120, oy + 32, &FONT_L, t, C_WHITE, 0);
    glo_text(120, oy + 54, &FONT_S, "BPM", GL_UI, 0);
    for (int i = 0; i < 16; i++) {
        float x = 14.0f + (float)(i / 2) * 27.0f;
        int32_t h = i % 4 == 0 ? 22 : i & 1 ? 9 : 14;
        if (i & 1)
            x += (13.5f * (float)(v->k[1] - 50) / 25.0f + 13.5f) * 0.5f + 6.0f;
        cv_rect((int32_t)x, oy + 92 - h, 9, h, v->pos >= 0 && i == v->pos % 16 ? PINK3[1] : i & 1 ? HM_GREEN[1] : HM_BLUE[1]);
    }
    e = glo_num(v->k[1], glo_cat(t, "SWING "));
    glo_cat(e, " %");
    glo_text(226, oy + 104, &FONT_XS, t, HM_GREEN[1], 1);
    float back = gl.tb < 0.25f ? 0.3f * (1.0f - gl.tb / 0.25f) : 0.0f;
    for (int j = 0; j < 2; j++) {
        int32_t y = oy + 122 + 18 * j, w = (int32_t)(170.0f * (float)v->k[2 + j] / 127.0f * (1.0f - back));
        uint16_t c = j ? HM_ORANGE[1] : HM_WHITE[1];
        glo_text(14, y + 3, &FONT_XS, j ? "DUCK" : "COMP", c, 2);
        cv_rect(56, y, 170, 6, SP_WIRE);
        cv_rect(226 - w, y, w, 6, c);
    }
}

/* a row of options: the legend in the encoder's colour, the names side by side in gray, the chosen one on a pad */
static void glo_opts(const char *label, const char *const *names, int n, int cur, int32_t y, uint16_t col)
{
    int32_t x = 62;
    glo_text(14, y + 4, &FONT_XS, label, col, 2);
    for (int i = 0; i < n; i++) {
        int32_t w = text_w(&FONT_XS, names[i]) + 8;
        if (i == cur)
            cv_rect(x, y - 3, w, 14, col);
        glo_text(x + 4, y + 4, &FONT_XS, names[i], i == cur ? SP_BAR : GL_UI, 2);
        x += w + 3;
    }
}

/* SCALE: the scale as a title, an octave of keys (the tonic blue, the mode's notes green, held pitch classes pink),
   QUANT and CLICK */
static void glo_scale(const q_glview *v, int32_t oy)
{
    static const uint8_t WH[7] = {0, 2, 4, 5, 7, 9, 11}, BK[5][2] = {{1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}};
    const int32_t w = 28, x0 = 22, y0 = oy + 42, h = 70, hb = 40;
    glo_text(120, oy + 22, &FONT_M, v->title, C_WHITE, 0);
    for (int i = 0; i < 7; i++) {
        int n = WH[i], lit = v->mask >> ((n - v->tonic + 12) % 12) & 1;
        uint16_t f = v->held >> n & 1 ? PINK3[1] : n == v->tonic ? HM_BLUE[1] : lit ? HM_GREEN[1] : 0;
        cv_rect(x0 + i * w + 1, y0, w - 2, h, f ? f : GL_KEY);
        glo_text(x0 + i * w + w / 2, y0 + h - 9, &FONT_S, v->notes[n], f ? SP_BAR : HM_WHITE[2], 0);
    }
    for (int i = 0; i < 5; i++) {
        int n = BK[i][0], lit = v->mask >> ((n - v->tonic + 12) % 12) & 1;
        uint16_t f = v->held >> n & 1 ? PINK3[1] : n == v->tonic ? HM_BLUE[1] : lit ? HM_GREEN[1] : 0;
        int32_t x = x0 + (BK[i][1] + 1) * w - 9;
        cv_rect(x, y0, 18, hb, f ? f : GL_BLACK);
        if (f)
            glo_text(x + 9, y0 + hb - 8, &FONT_XS, "#", SP_BAR, 0);
    }
    glo_opts("QUANT", v->opt[0], v->nopt[0], v->k[2], oy + 126, HM_WHITE[1]);
    glo_opts("CLICK", v->opt[1], v->nopt[1], v->k[3], oy + 146, HM_ORANGE[1]);
}

/* KEYS: IN SCALE, what it means, and the 16 white keys with the note each plays (in the scale: dark blue, the tonic
   blue; else ordinary), the black keys where a piano has them, crossed out in the scale; the held key pink */
static void glo_keys(const q_glview *v, int32_t oy)
{
    static const char *const SAY[3] = {"CHROMATIC KEYBOARD", "SCALE ON MARBLES TRACKS", "SCALE ON EVERY TRACK"};
    glo_opts("IN SCALE", v->opt[0], v->nopt[0], v->k[0], oy + 14, HM_BLUE[1]);
    glo_text(120, oy + 42, &FONT_S, SAY[v->k[0]], GL_INK, 0);
    if (v->inscale)
        glo_text(120, oy + 56, &FONT_S, v->title, GL_UI, 0);
    for (int i = 0; i < 16; i++) {      /* 13.5 px a key */
        int n = v->keyn[i] % 12, hit = v->held >> i & 1, lit = v->inscale && v->keyn[i] >= 0;
        int32_t x = 12 + i * 27 / 2;
        uint16_t f = hit ? PINK3[1] : lit ? (n == v->tonic ? HM_BLUE[1] : HM_BLUE[2]) : GL_KEY, ink = hit || !lit ? SP_BAR : GL_INK;
        cv_rect(x + 1, oy + 70, 12, 76, f);
        if (v->keyn[i] < 0)
            continue;
        char l[2] = {v->notes[n][0], 0};
        glo_text(x + 7, oy + 136, &FONT_S, l, ink, 0);
        if (v->notes[n][1])
            glo_text(x + 7, oy + 126, &FONT_XS, "#", ink, 0);
    }
    for (int i = 0; i < 15; i++) {      /* the black keys, where a piano has them: the FM-1's keyboard starts on F (plat.h), */
        int32_t x = 12 + (i + 1) * 27 / 2 - 4;      /* so none between B and C (gap 3) nor E and F (gap 6) */
        if (i % 7 == 3 || i % 7 == 6)
            continue;
        cv_rect(x, oy + 70, 9, 44, v->inscale ? C_BLACK : GL_BLACK);
        if (v->inscale) {
            cv_line(x + 1, oy + 72, x + 8, oy + 112, SP_WIRE);
            cv_line(x + 8, oy + 72, x + 1, oy + 112, SP_WIRE);
        }
    }
}

/* FX: DELAY (a note and its echoes on a time line, spaced by the division, each lower by the feedback) and REVERB (a
   filled triangle as long as SIZE, as dark as TONE); a note passes pink from echo to echo, then sweeps the tail */
static void glo_fx(const q_glview *v, int32_t oy)
{
    static const uint8_t SP[9] = {8, 10, 13, 17, 22, 30, 40, 60, 90};
    char t[24];
    glo_text(14, oy + 14, &FONT_S, "DELAY", HM_BLUE[1], 2);
    glo_num(v->k[1], glo_cat(glo_cat(t, v->title), "  -  FB "));
    glo_text(226, oy + 14, &FONT_S, t, GL_UI, 1);
    float fall = 0.15f + 0.8f * (float)v->k[1] / 127.0f;
    cv_rect(14, oy + 78, 212, 1, SP_WIRE);
    for (int k = 0, x = 16, h = 46; x < 224 && h > 2; k++, x += SP[v->k[0]], h = (int)((float)h * fall)) {
        int lit = gl.tn >= 0.12f * (float)k && gl.tn < 0.12f * (float)(k + 1);
        cv_rect(x, oy + 78 - h, 5, h, lit ? PINK3[1] : k ? HM_BLUE[1] : GL_INK);
    }
    glo_text(14, oy + 98, &FONT_S, "REVERB", HM_WHITE[1], 2);
    glo_num(v->k[3], glo_cat(glo_num(v->k[2], glo_cat(t, "SIZE ")), "  -  TONE "));
    glo_text(226, oy + 98, &FONT_S, t, GL_UI, 1);
    float len = 30.0f + 180.0f * (float)v->k[2] / 127.0f, sweep = gl.tn < 1.2f ? len * gl.tn / 1.2f : -1.0f;
    uint16_t c = p3d_mix(0, HM_ORANGE[0], 0.35f + 0.65f * (float)v->k[3] / 127.0f);
    for (int x = 0; (float)x < len; x += 2) {
        int32_t h = (int32_t)(46.0f * (1.0f - ((float)x + 1.0f) / len));
        cv_rect(16 + x, oy + 156 - h, 2, h, (float)x < sweep ? PINK3[2] : c);
    }
}

/* SONG: the whole song as a strip (an entry as long as its repeats; the cursor blue, the one playing pink), then 5 rows
   around the cursor: number, scene, a box per repeat */
static void glo_song(const q_glview *v, int32_t oy)
{
    char t[16];
    int n = v->song.n, total = 0, first, cum = 0;
    for (int i = 0; i < n; i++)
        total += v->song.e[i].reps;
    glo_cat(glo_num(n, t), " ENTRIES");
    glo_text(14, oy + 12, &FONT_S, t, GL_INK, 2);
    glo_text(226, oy + 12, &FONT_S, v->song.on ? "SONG ON" : "SONG OFF", v->song.on ? HM_ORANGE[1] : GL_UI, 1);
    if (!n) {
        glo_text(120, oy + 90, &FONT_M, "EMPTY", GL_UI, 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        int32_t x0 = 14 + 212 * cum / total, x1 = 14 + 212 * (cum + v->song.e[i].reps) / total;
        cum += v->song.e[i].reps;
        cv_rect(x0, oy + 24, x1 - x0 - 1, 10, v->song.on && i + 1 == v->song.playing ? PINK3[1] : i + 1 == v->song.cursor ? HM_BLUE[1] : HM_GREEN[1 + (i & 1)]);
    }
    first = v->song.cursor - 3 < n - 5 ? v->song.cursor - 3 : n - 5;
    if (first < 0)
        first = 0;
    for (int k = 0; k < 5 && first + k < n; k++) {
        int i = first + k, cur = i + 1 == v->song.cursor;
        int32_t y = oy + 46 + 22 * k;
        if (cur)
            cv_rect(8, y - 2, 224, 20, RGB(0x1a, 0x16, 0x36)), cv_rect(8, y - 2, 3, 20, HM_BLUE[1]);
        glo_text(26, y + 8, &FONT_S, glo_num(i + 1, t), cur ? HM_BLUE[1] : GL_UI, 1);
        cv_rect(36, y + 1, 24, 14, v->song.on && i + 1 == v->song.playing ? PINK3[1] : HM_GREEN[1]);
        glo_num(v->song.e[i].scene + 1, glo_cat(t, "S"));
        glo_text(48, y + 8, &FONT_S, t, SP_BAR, 0);
        for (int q = 0; q < v->song.e[i].reps; q++)
            cv_rect(70 + q * 17 / 2, y + 4, 6, 8, HM_WHITE[2]);
        glo_num(v->song.e[i].reps, glo_cat(t, "x"));
        glo_text(226, y + 8, &FONT_S, t, cur ? GL_INK : GL_UI, 1);
    }
}

void glo_band(const q_glview *v, int32_t oy)
{
    cv_rect(0, oy, 240, 166, C_BLACK);
    switch (v->page) {
    case GP_TEMPO: glo_tempo(v, oy); break;
    case GP_SCALE: glo_scale(v, oy); break;
    case GP_KEYS: glo_keys(v, oy); break;
    case GP_FX: glo_fx(v, oy); break;
    case GP_SONG: glo_song(v, oy); break;
    }
}

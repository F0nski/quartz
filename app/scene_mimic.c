/* MIMIC's two rolls (spec MIMIC section 5; docs/screens/mimic.html, the reading head in colours): the phrase above
 * (YOU), cut in four coloured quarters and read by a pink head that jumps; the improvisation below (MIMIC), born on the
 * NOW line and scrolling left, each event at the colour of the quarter it quotes. One row per distinct note of the
 * phrase, shared by both rolls. Part of the unity build after scene_orbits.c (app_ui.c): KBLUE, KGREEN, ORANGE,
 * MB_WHITE, PINK3, glo_text, orv_num. */

#define MV_X0 30                    /* the rolls' left edge (the note names at the left of it) */
#define MV_W 202
#define MV_PH0 24                   /* YOU: the phrase */
#define MV_PH1 76
#define MV_IM0 94                   /* MIMIC: the improvisation */
#define MV_IM1 158
#define MV_NOW 196
#define MV_SC 8                     /* px a step in the MIMIC roll */
#define MV_TRAIL 24
#define MV_PLAY_MS 160u
#define MV_JUMP_MS 600u
#define MV_SEED_MS 300u
#define MV_ROW RGB(0x0e, 0x0a, 0x22)
#define MV_MUTE RGB(0x4a, 0x44, 0x66)
#define MV_TXT RGB(0xc9, 0xc3, 0xe4)
#define MV_WIRE RGB(0x3a, 0x2f, 0x63)

static struct {
    int seen, nt;                   /* nt: events in the trail */
    uint32_t at, n, t_at, t_n, ms, seed_t;
    uint8_t seed;
    struct { uint8_t place, note, jump; uint32_t at; } trail[MV_TRAIL];   /* the oldest first */
} mv;

void mimic_enter(void) { mv.seen = 0; }

static const uint16_t *mv_tones(const q_mimic *m, int place) { int q = place * 4 / m->n; return q == 0 ? KBLUE : q == 1 ? KGREEN : q == 2 ? ORANGE : MB_WHITE; }
static int mv_gap(const q_mimic *m, int place) { return place < m->n && m->ev[place][1] ? m->ev[place][1] : 1; }

/* the phrase's distinct notes, ascending; their number */
static int mv_rows(const q_mimic *m, uint8_t *rows)
{
    int nr = 0;
    for (int i = 0; i < m->n; i++) {
        int j = 0, note = m->ev[i][0];
        while (j < nr && rows[j] < note) j++;
        if (j < nr && rows[j] == note) continue;
        for (int k = nr; k > j; k--) rows[k] = rows[k - 1];
        rows[j] = (uint8_t)note, nr++;
    }
    return nr;
}
/* the y of note's row in the roll y0..y1 (rows of (y1 - y0) / nr px, the lowest at the bottom) */
static float mv_y(const uint8_t *rows, int nr, int note, int y0, int y1)
{
    int i = 0;
    while (i < nr - 1 && rows[i] != note) i++;
    return (float)y1 - ((float)i + 0.5f) * (float)(y1 - y0) / (float)nr;
}
/* place i's cell in the phrase: x and width, each as wide as its gap (0 counts 1) */
static void mv_cell(const q_mimic *m, int i, float *x, float *w)
{
    int tot = 0, before = 0;
    for (int j = 0; j < m->n; j++) {
        if (j == i) before = tot;
        tot += mv_gap(m, j);
    }
    *x = (float)MV_X0 + (float)before * (float)MV_W / (float)tot, *w = (float)mv_gap(m, i) * (float)MV_W / (float)tot;
}
/* a note's name ("C4"), or a KIT's pad ("BD", "P4") */
static void mv_name(int note, int kit, char *b)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const PADS3[3] = {"BD", "SN", "HH"};
    const char *s;
    if (kit) {
        int pad = ((note - KIT_NOTE) % 16 + 16) % 16;
        if (pad < 3) { s = PADS3[pad]; while ((*b++ = *s++) != 0) {} return; }
        *b++ = 'P';
        orv_num(pad + 1, b);
        return;
    }
    s = N[note % 12];
    while (*s) *b++ = *s++;
    if (note < 12) { *b++ = '-'; *b++ = '1'; *b = 0; }
    else orv_num(note / 12 - 1, b);
}
/* a flat cell: its face, a light lip on top */
static void mv_rect(float x, float y, float w, float h, const uint16_t *t, int32_t oy)
{
    int32_t x0 = (int32_t)x, x1 = (int32_t)(x + w) - 1, y0 = (int32_t)(y - h * 0.5f);
    if (x1 <= x0) x1 = x0 + 1;
    cv_rect(x0, oy + y0, x1 - x0, (int32_t)h, t[1]);
    cv_rect(x0, oy + y0, x1 - x0, 1, t[0]);
}
/* the head's place in the phrase: its cell's left edge, moved on with the time of its gap */
static int mv_head(const q_miview *v, float *hx)
{
    if (v->listen || !v->playing || !v->r.nlast || v->r.last[0] >= v->m.n) return 0;
    float x, w, f = v->step_ms ? (float)(mv.ms - mv.t_at) / ((float)v->step_ms * (float)mv_gap(&v->m, v->r.last[0])) : 1.0f;
    mv_cell(&v->m, v->r.last[0], &x, &w);
    *hx = x + (f > 1.0f ? 1.0f : f) * w;
    return 1;
}

void mimic_frame(const q_miview *v, uint32_t ms)
{
    const q_mimic_run *r = &v->r;
    if (!mv.seen) mv.seed = v->m.seed, mv.seed_t = ms - 100000u, mv.nt = 0;
    if (v->m.seed != mv.seed) mv.seed = v->m.seed, mv.seed_t = ms;
    if (!v->playing || v->listen || r->at < mv.at) mv.nt = 0;   /* stopped, learning, or a reset: the trail is gone */
    if (v->playing && !v->listen && r->nlast && (!mv.seen || r->at != mv.at || !mv.nt)) {   /* a step played: its events */
        for (int i = 0; i < r->nlast; i++) {
            if (mv.nt == MV_TRAIL) {
                for (int k = 1; k < MV_TRAIL; k++) mv.trail[k - 1] = mv.trail[k];
                mv.nt--;
            }
            int e = r->last[i];
            mv.trail[mv.nt].place = (uint8_t)e, mv.trail[mv.nt].note = e < v->m.n ? v->m.ev[e][0] : 0;
            mv.trail[mv.nt].jump = (uint8_t)(r->jumps >> i & 1), mv.trail[mv.nt].at = r->at;
            mv.nt++;
        }
        mv.t_at = mv.seen ? ms : ms - 100000u;
    }
    if (!mv.seen || r->n != mv.n) mv.t_n = ms;
    mv.at = r->at, mv.n = r->n, mv.ms = ms, mv.seen = 1;
    p3d_begin();
    float hx;
    int fr = r->from, e = r->last[0];
    if (mv_head(v, &hx) && fr >= 0 && fr < v->m.n && e != (fr + 1) % v->m.n && ms - mv.t_at < MV_JUMP_MS) {
        float ax, aw, bx, bw;                   /* a jump: the arrow from the end of the place left to the place read */
        mv_cell(&v->m, fr, &ax, &aw);
        mv_cell(&v->m, e, &bx, &bw);
        ax += aw;
        float d = bx > ax ? bx - ax : ax - bx, top = (float)MV_PH0 - 8.0f - (d * 0.15f < 14.0f ? d * 0.15f : 14.0f);
        float base = (float)MV_PH0 - 4.0f, px = ax, py = base;
        uint16_t c = r->jumps & 1 ? PINK3[1] : MV_TXT;
        for (int q = 1; q <= 12; q++) {
            float t = (float)q / 12.0f, x = ax + (bx - ax) * t, y = base - (base - top) * fm_sinf(3.1415927f * t);
            p3d_line(px, py, x, y, 0.0f, 2, c);
            px = x, py = y;
        }
        p3d_tri(bx, (float)MV_PH0 - 3.0f, bx - 3.0f, (float)MV_PH0 - 8.0f, bx + 3.0f, (float)MV_PH0 - 8.0f, 1.0f, c);
    }
}

void mimic_band(const q_miview *v, int32_t oy)
{
    const q_mimic *m = &v->m;
    uint8_t rows[MIMIC_EV];
    int nr = mv_rows(m, rows), i;
    char b[12];
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    for (i = 0; i < 40; i++)                    /* the sky */
        cv_rect((int32_t)((i * 733 + 170) / 10 % 240), oy + 4 + (int32_t)((i * 479 + 40) / 10 % 160), 1, 1,
                i % 4 ? RGB(0x1c, 0x13, 0x40) : RGB(0x3a, 0x2f, 0x63));
    glo_text(8, oy + 21, &FONT_XS, "YOU", MV_MUTE, 2);
    glo_text(8, oy + MV_IM0 - 6, &FONT_XS, "MIMIC", MV_MUTE, 2);
    cv_rect(8, oy + 85, 224, 1, RGB(0x1c, 0x13, 0x40));
    if (m->n) {
        float rh = (float)(MV_PH1 - MV_PH0) / (float)nr, ch = rh - 1.0f < 6.0f ? rh - 1.0f : 6.0f;
        if (ch < 2.0f) ch = 2.0f;
        int cur = v->listen ? m->n - 1 : -1;
        if (!v->listen && v->playing && v->r.nlast && mv.ms - mv.t_at < MV_PLAY_MS) cur = v->r.last[0];
        for (i = 0; i < nr; i++) {              /* the rows, named at the left when they fit */
            int note = rows[i];
            int32_t y0 = (int32_t)mv_y(rows, nr, note, MV_PH0, MV_PH1), y1 = (int32_t)mv_y(rows, nr, note, MV_IM0, MV_IM1);
            cv_rect(MV_X0, oy + y0, MV_W, 1, MV_ROW);
            cv_rect(MV_X0, oy + y1, MV_W, 1, MV_ROW);
            if (rh >= 7.0f) {
                mv_name(note, v->kit, b);
                glo_text(26, oy + y0, &FONT_XS, b, MV_MUTE, 1);
                glo_text(26, oy + y1, &FONT_XS, b, MV_MUTE, 1);
            }
        }
        for (int q = 0; q < 4; q++) {           /* the quarters, marked under the phrase */
            int i0 = (q * m->n + 3) / 4, i1 = ((q + 1) * m->n + 3) / 4 - 1;
            if (i1 < i0) continue;
            float x0, w0, x1, w1;
            mv_cell(m, i0, &x0, &w0);
            mv_cell(m, i1, &x1, &w1);
            const uint16_t *t = mv_tones(m, i0);
            cv_rect((int32_t)x0, oy + MV_PH1 + 3, (int32_t)(x1 + w1) - 1 - (int32_t)x0, 2, t[2]);
        }
        for (i = 0; i < m->n; i++) {            /* the phrase */
            float x, w;
            mv_cell(m, i, &x, &w);
            mv_rect(x, mv_y(rows, nr, m->ev[i][0], MV_PH0, MV_PH1), w, ch, i == cur ? PINK3 : mv_tones(m, i), oy);
        }
        if (v->playing && !v->listen) {
            float now = v->r.n ? (float)(v->r.n - 1) : 0.0f, f = v->step_ms ? (float)(mv.ms - mv.t_n) / (float)v->step_ms : 0.0f;
            now += f > 1.0f ? 1.0f : f;
            cv_rect(MV_NOW, oy + MV_IM0 - 4, 1, MV_IM1 - MV_IM0 + 6, MV_WIRE);
            glo_text(MV_NOW, oy + MV_IM0 - 8, &FONT_XS, "NOW", MV_MUTE, 0);
            if (m->loop)                        /* LOOP: where the draw starts again, dotted */
                for (uint32_t st = (uint32_t)now / m->loop * m->loop; ; st -= m->loop) {
                    int32_t x = MV_NOW - (int32_t)((now - (float)st) * MV_SC);
                    if (x <= MV_X0) break;
                    for (int32_t y = MV_IM0; y < MV_IM1; y += 5) cv_rect(x, oy + y, 1, 2, MV_WIRE);
                    if (st < m->loop) break;
                }
            for (i = 0; i < mv.nt; i++) {       /* the improvisation */
                float x = (float)MV_NOW - (now - (float)mv.trail[i].at) * MV_SC, w = (float)(mv_gap(m, mv.trail[i].place) * MV_SC);
                if (x + w <= (float)MV_X0 || mv.trail[i].place >= m->n) continue;
                if (x < (float)MV_X0) w -= (float)MV_X0 - x, x = (float)MV_X0;
                float y = mv_y(rows, nr, mv.trail[i].note, MV_IM0, MV_IM1);
                int hot = mv.trail[i].at == mv.at && mv.ms - mv.t_at < MV_PLAY_MS;
                mv_rect(x, y, w, ch, hot ? PINK3 : mv_tones(m, mv.trail[i].place), oy);
                if (mv.trail[i].jump)           /* from DRIFT: a pink notch above */
                    cv_rect((int32_t)x, oy + (int32_t)(y - ch * 0.5f) - 3, 3, 2, PINK3[1]);
                if (i >= mv.nt - m->order) {    /* ORDER: the context looked for in the phrase, bracketed */
                    int32_t bx = (int32_t)x - 2, by0 = (int32_t)(y - ch * 0.5f) - 3, by1 = (int32_t)(y + ch * 0.5f) + 3;
                    cv_rect(bx, oy + by0, 1, by1 - by0, C_WHITE);
                    cv_rect(bx, oy + by0, 4, 1, C_WHITE);
                    cv_rect(bx, oy + by1, 4, 1, C_WHITE);
                }
            }
        }
    }
    p3d_draw(oy);
    float hx;
    if (m->n && mv_head(v, &hx)) {              /* the reading head */
        cv_rect((int32_t)hx - 1, oy + MV_PH0 - 2, 2, MV_PH1 - MV_PH0 + 4, PINK3[1]);
        p3d_fill(hx - 4.0f, (float)(oy + MV_PH0 - 6), hx + 4.0f, (float)(oy + MV_PH0 - 6), hx, (float)(oy + MV_PH0 - 2), PINK3[1]);
    }
    orv_num(m->n, b);                           /* the top strip: the phrase's size, LOOP, DRIFT, the seed */
    for (i = 0; b[i]; i++) {}
    __builtin_memcpy(b + i, " NOTES", 7);
    glo_text(8, oy + 10, &FONT_XS, b, MV_TXT, 2);
    if (m->loop) {
        char *p = b;
        __builtin_memcpy(p, "LOOP ", 5), p += 5;
        orv_num(v->playing && v->r.n ? (int)((v->r.n - 1) % m->loop) + 1 : 1, p);
        while (*p) p++;
        *p++ = '/';
        orv_num(m->loop, p);
        glo_text(74, oy + 10, &FONT_XS, b, C_WHITE, 2);
    } else
        glo_text(74, oy + 10, &FONT_XS, "FREE", MV_MUTE, 2);
    for (i = 0; i < 8; i++) {                   /* DRIFT, in 8 diamonds */
        float x = 140.0f + 7.0f * (float)i, y = (float)(oy + 11);
        uint16_t c = i < (m->drift + 8) / 16 ? PINK3[1] : RGB(0x2a, 0x21, 0x48);
        p3d_fill(x, y - 3.0f, x + 3.0f, y, x - 3.0f, y, c);
        p3d_fill(x + 3.0f, y, x, y + 3.0f, x - 3.0f, y, c);
    }
    b[0] = '#';
    orv_num(m->seed, b + 1);
    glo_text(234, oy + 10, &FONT_XS, b, mv.ms - mv.seed_t < MV_SEED_MS ? PINK3[1] : MV_MUTE, 1);
}

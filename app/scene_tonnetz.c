/* TONNETZ's map (spec TONNETZ section 6; docs/screens/tonnetz.html, T1 without a keyboard): the Tonnetz's lattice with
 * the note names at its nodes, the chord playing a pink triangle of three facets, the three before it blue traces; the
 * map slides to keep the chord at the centre. The chord's nodes carry the notes played with their octaves, light one
 * after the other with STRUM, the bass marked below (VOICE). On top, the steps 1-8 and RATE's notches, or the held
 * step's banner. Positions are relative to the chord, so a long walk stays exact. Part of the unity build after
 * scene_chords.c (app_ui.c): KBLUE, PINK3, MB_WHITE, ORANGE, glo_text. */

#define TZV_DX 46.0f                /* the lattice: node (i, j) at (46 i + 23 j, -40 j) */
#define TZV_DY 40.0f
#define TZV_CX 120.0f               /* the chord's centre on the screen */
#define TZV_CY 92.0f
#define TZV_SLIDE_MS 250u
#define TZV_HIT_MS 140u
#define TZV_EDGE RGB(0x1c, 0x13, 0x40)
#define TZV_NODE RGB(0x2a, 0x52, 0xc8)
#define TZV_OUT RGB(0x1c, 0x13, 0x40)
#define TZV_UI RGB(0x4a, 0x44, 0x66)
#define TZV_OUT_UI RGB(0x24, 0x18, 0x50)
#define TZV_BANNER RGB(0xe4, 0xe0, 0xf5)
static const uint16_t TZV_TRAIL[3] = {RGB(0x2a, 0x52, 0xc8), RGB(0x1c, 0x35, 0x90), RGB(0x16, 0x25, 0x5e)};
static const uint16_t TZV_HOT[3] = {RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90), RGB(0xe4, 0xe0, 0xf5)};
static const char *const TZV_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const TZV_OP[4] = {"-", "P", "L", "R"};

static struct {
    int seen;
    q_tz cur;
    float fx, fy;                   /* the slide's start: the old chord's centre minus the new one's */
    uint32_t move_ms, hit_ms, ms;
    uint8_t strikes;
} tzv;

void tonnetz_enter(void) { tzv.seen = 0; }

static int tzv_pc(int i, int j, int base) { return ((7 * i + 4 * j + base) % 12 + 12) % 12; }

static void tzv_nodes(q_tz s, int n[3][2])
{
    static const int8_t MAJ[3][2] = {{0, 0}, {1, 0}, {0, 1}}, MIN[3][2] = {{1, 0}, {0, 1}, {1, 1}};
    const int8_t (*d)[2] = s.m ? MIN : MAJ;
    for (int k = 0; k < 3; k++) n[k][0] = s.i + d[k][0], n[k][1] = s.j + d[k][1];
}

/* a triad's centre in lattice pixels, relative to the node o */
static void tzv_centre(q_tz s, q_tz o, float *x, float *y)
{
    int n[3][2];
    tzv_nodes(s, n);
    *x = 0.0f, *y = 0.0f;
    for (int k = 0; k < 3; k++) {
        float di = (float)(n[k][0] - o.i), dj = (float)(n[k][1] - o.j);
        *x += (TZV_DX * di + 0.5f * TZV_DX * dj) / 3.0f, *y += -TZV_DY * dj / 3.0f;
    }
}

/* node (cur.i + di, cur.j + dj) on screen, the slide applied */
static void tzv_at(const q_tzview *v, int di, int dj, float *x, float *y)
{
    float cx, cy, u = tzv.ms - tzv.move_ms >= TZV_SLIDE_MS ? 1.0f : (float)(tzv.ms - tzv.move_ms) / (float)TZV_SLIDE_MS;
    tzv_centre(v->cur, v->cur, &cx, &cy);
    *x = TZV_CX + TZV_DX * (float)di + 0.5f * TZV_DX * (float)dj - cx + tzv.fx * (1.0f - u);
    *y = TZV_CY - TZV_DY * (float)dj - cy + tzv.fy * (1.0f - u);
}

static void tzv_tri(const q_tzview *v, q_tz s, float z, const uint16_t *tones, int facets)
{
    int n[3][2];
    float x[3], y[3];
    tzv_nodes(s, n);
    for (int k = 0; k < 3; k++) tzv_at(v, n[k][0] - v->cur.i, n[k][1] - v->cur.j, &x[k], &y[k]);
    if (!facets) {
        p3d_tri(x[0], y[0], x[1], y[1], x[2], y[2], z, tones[0]);
        return;
    }
    float cx = (x[0] + x[1] + x[2]) / 3.0f, cy = (y[0] + y[1] + y[2]) / 3.0f;
    for (int k = 0; k < 3; k++) p3d_tri(x[k], y[k], x[(k + 1) % 3], y[(k + 1) % 3], cx, cy, z, tones[k]);
}

void tonnetz_frame(const q_tzview *v, uint32_t ms)
{
    if (tzv.seen && (v->cur.i != tzv.cur.i || v->cur.j != tzv.cur.j || v->cur.m != tzv.cur.m)) {   /* a new chord: slide to it */
        float ox, oy, nx, ny, u = ms - tzv.move_ms >= TZV_SLIDE_MS ? 1.0f : (float)(ms - tzv.move_ms) / (float)TZV_SLIDE_MS;
        tzv_centre(tzv.cur, v->cur, &ox, &oy), tzv_centre(v->cur, v->cur, &nx, &ny);
        tzv.fx = ox - nx + tzv.fx * (1.0f - u), tzv.fy = oy - ny + tzv.fy * (1.0f - u);   /* from where the map shows now */
        if (tzv.fx * tzv.fx + tzv.fy * tzv.fy > 200.0f * 200.0f)   /* a far jump (RETURN after a long walk): a cut */
            tzv.fx = tzv.fy = 0.0f;
        tzv.move_ms = ms;
    }
    if (!tzv.seen)
        tzv.fx = tzv.fy = 0.0f, tzv.move_ms = ms - 100000u;
    if (!tzv.seen || v->strikes != tzv.strikes)
        tzv.strikes = v->strikes, tzv.hit_ms = tzv.seen ? ms : ms - 100000u;
    tzv.cur = v->cur, tzv.seen = 1, tzv.ms = ms;
    p3d_begin();
    if (v->kit)
        return;
    for (int dj = -5; dj <= 5; dj++)                /* the lattice's edges on the map (wide: a slide shows more of it) */
        for (int di = -8; di <= 8; di++) {
            float x0, y0, x1, y1, x2, y2;
            tzv_at(v, di, dj, &x0, &y0), tzv_at(v, di + 1, dj, &x1, &y1), tzv_at(v, di, dj + 1, &x2, &y2);
            if (y0 < 0.0f || y0 > 210.0f || x0 < -60.0f || x0 > 300.0f)
                continue;
            p3d_line(x0, y0, x1, y1, 0.0f, 1, TZV_EDGE);
            p3d_line(x0, y0, x2, y2, 0.0f, 1, TZV_EDGE);
            p3d_line(x1, y1, x2, y2, 0.0f, 1, TZV_EDGE);
        }
    for (int k = 2; k >= 0; k--)                    /* the three chords before, fading */
        if (v->trail[k].m <= 1)
            tzv_tri(v, v->trail[k], 0.5f, &TZV_TRAIL[k], 0);
    int hot = v->playing && ms - tzv.hit_ms < TZV_HIT_MS;
    tzv_tri(v, v->cur, 1.0f, !v->playing ? KBLUE : hot ? TZV_HOT : PINK3, 1);
}

void tonnetz_band(const q_tzview *v, int32_t oy)
{
    int i;
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    if (v->kit) {
        glo_text(120, oy + 80, &FONT_S, "NO CHORDS ON A KIT", MB_WHITE[1], 0);
        return;
    }
    p3d_draw(oy);
    int n[3][2];
    tzv_nodes(v->cur, n);
    for (int dj = -5; dj <= 5; dj++)                /* the nodes and their notes */
        for (int di = -8; di <= 8; di++) {
            float fx, fy;
            tzv_at(v, di, dj, &fx, &fy);
            int32_t x = (int32_t)fx, y = (int32_t)fy;
            if (y < 24 || y > 162 || x < -6 || x > 246)
                continue;
            int pc = tzv_pc(v->cur.i + di, v->cur.j + dj, v->base), k = -1, in = 0;
            for (int q = 0; q < 3; q++)
                in |= n[q][0] == v->cur.i + di && n[q][1] == v->cur.j + dj;
            if (in) {                               /* a chord note: its octave, lit as it sounds, the bass marked */
                for (int q = 0; q < v->n; q++)
                    if (v->notes[q] % 12 == pc) k = q;
                uint32_t at = tzv.hit_ms + (uint32_t)(k < 0 ? 0 : k) * STRUM_TICKS[v->tz.strum] * v->step_ms / 4800u;
                int lit = v->playing && k >= 0 && (int32_t)(tzv.ms - at) >= 0 && tzv.ms - at < TZV_HIT_MS, r = lit ? 6 : 4;
                uint16_t c = lit ? MB_WHITE[0] : MB_WHITE[1];
                cv_rect(x - 1, oy + y - r, 3, 2 * r + 1, c);
                cv_rect(x - r, oy + y - 1, 2 * r + 1, 3, c);
                char b[5] = {0};
                const char *s = TZV_NOTE[pc];
                int bi = 0;
                while (*s) b[bi++] = *s++;
                if (k >= 0 && v->notes[k] >= 12) b[bi] = (char)('0' + v->notes[k] / 12 - 1);   /* octave: C4 = 60 */
                else if (k >= 0) b[bi] = '-', b[bi + 1] = '1';
                glo_text(x, oy + y - 10, &FONT_XS, b, C_WHITE, 0);
                if (k == 0)
                    p3d_fill((float)x - 4.0f, (float)(oy + y + 8), (float)x + 4.0f, (float)(oy + y + 8), (float)x, (float)(oy + y + 13), MB_WHITE[0]);
                continue;
            }
            int ok = !v->tz.scale || (v->scale >> pc & 1);
            cv_rect(x - 1, oy + y - 3, 3, 7, ok ? TZV_NODE : TZV_OUT);
            cv_rect(x - 3, oy + y - 1, 7, 3, ok ? TZV_NODE : TZV_OUT);
            glo_text(x, oy + y - 8, &FONT_XS, TZV_NOTE[pc], ok ? TZV_UI : TZV_OUT_UI, 0);
        }
    float cx = 0.0f, cy = 0.0f;                     /* the chord's name at its centre */
    for (int q = 0; q < 3; q++) {
        float x, y;
        tzv_at(v, n[q][0] - v->cur.i, n[q][1] - v->cur.j, &x, &y);
        cx += x / 3.0f, cy += y / 3.0f;
    }
    int hot = v->playing && tzv.ms - tzv.hit_ms < TZV_HIT_MS;
    glo_text((int32_t)cx, oy + (int32_t)cy, &FONT_S, v->name, hot ? RGB(0, 0, 0) : C_WHITE, 0);
    cv_rect(0, oy, 240, 19, RGB(0, 0, 0));          /* the top strip over the map */
    for (i = 0; i < 8; i++) {                       /* the steps */
        int on = i < v->tz.len, held = i == v->held, app = v->playing && i == v->applied;
        uint16_t c = !on ? RGB(0x12, 0x0c, 0x2a) : held ? TZV_BANNER : app ? (v->chaos_hit ? ORANGE[1] : PINK3[1]) : RGB(0x1c, 0x35, 0x90);
        cv_rect(6 + 15 * i, oy + 4, 13, 12, c);
        if (on)
            glo_text(12 + 15 * i, oy + 10, &FONT_XS, TZV_OP[TZ_OP(&v->tz, i)], held ? RGB(0, 0, 0) : C_WHITE, 0);
    }
    if (v->tz.ret) {                                /* RETURN: back to the start after the last step */
        float x = (float)(6 + 15 * v->tz.len + 6), y = (float)(oy + 10);
        p3d_fill(x, y - 5.0f, x + 5.0f, y, x, y + 5.0f, ORANGE[1]);
        p3d_fill(x, y - 5.0f, x, y + 5.0f, x - 5.0f, y, ORANGE[1]);
    }
    if (v->held >= 0) {                             /* the held step's banner */
        cv_rect(124, oy + 3, 110, 12, TZV_BANNER);
        glo_text(179, oy + 9, &FONT_XS, v->banner, RGB(0, 0, 0), 0);
    } else {                                        /* RATE's notches, lit up to the strike in progress */
        int r = 1 << v->tz.rate, done = v->playing ? v->step / (16 / r) : -1;
        for (i = 0; i < r; i++)
            cv_rect(170 + (int32_t)(((float)i + 0.5f) * 62.0f / (float)r) - 1, oy + 12, 3, 3, i <= done ? PINK3[1] : TZV_EDGE);
    }
}

/* CHORDS' heptagon (spec CHORDS section 6; docs/screens/chords-source.html, direction C1 and its held mode): the seven
 * degrees of the scale on a heptagon, the chord playing a pink polygon over its degrees, the one before a wire ghost;
 * the progression in squares, RATE's notches, a keyboard of the notes struck. A chord held on the grid shows in white
 * instead, with its banner. The squares, notches, banner and keyboard go straight into the band (no primitives).
 * Part of the unity build after scene_lanes.c (app_ui.c): KBLUE, PINK3, MB_WHITE, glo_text. */

#define CHV_CX 120.0f               /* the heptagon in the scene area */
#define CHV_CY 78.0f
#define CHV_R 46.0f
#define CHV_HIT_MS 140u             /* a strike lights white this long */
#define CHV_KY 140                  /* the keyboard's top */
#define CHV_GHOST RGB(0x3a, 0x2f, 0x63)
#define CHV_EDGE RGB(0x2a, 0x21, 0x48)
#define CHV_UI RGB(0x4a, 0x44, 0x66)
#define CHV_KEY RGB(0x3a, 0x2f, 0x63)
#define CHV_BLACK RGB(0x12, 0x0c, 0x2a)
#define CHV_BANNER RGB(0xe4, 0xe0, 0xf5)
static const uint8_t CHV_STACK[CS_N][4] = {{0, 2, 4, 0}, {0, 2, 4, 6}, {0, 1, 4, 0}, {0, 3, 4, 0}};
static const char *const CHV_ROMAN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

static struct {
    int seen;
    uint8_t strikes;
    uint32_t hit_ms, ms;
} chv;

void chords_enter(void) { chv.seen = 0; }

/* degree d's corner at radius r */
static void chv_at(int d, float r, float *x, float *y)
{
    float a = -1.5707963f + (float)d * (6.2831853f / 7.0f);
    *x = CHV_CX + r * fm_cosf(a), *y = CHV_CY + r * fm_sinf(a);
}

/* a chord's degrees 0..6 (3 or 4 of them); returns their number */
static int chv_degrees(uint8_t ch, int out[4])
{
    int sz = CH_SIZE(ch), m = sz == CS_SEVENTH ? 4 : 3;
    for (int i = 0; i < m; i++) out[i] = (CH_DEG(ch) + CHV_STACK[sz][i]) % 7;
    return m;
}

static int chv_shown(const q_chview *v) { return v->held >= 0 ? v->held : v->slot; }

void chords_frame(const q_chview *v, uint32_t ms)
{
    if (!chv.seen || v->strikes != chv.strikes)     /* a strike: the notes light from here; arriving, nothing lights */
        chv.strikes = v->strikes, chv.hit_ms = chv.seen ? ms : ms - 100000u;
    chv.seen = 1;
    chv.ms = ms;
    p3d_begin();
    if (v->kit)
        return;
    int s = chv_shown(v), d[4], n, i;
    float x0, y0, x1, y1;
    for (i = 0; i < 7; i++) {                       /* the edges */
        chv_at(i, CHV_R, &x0, &y0), chv_at((i + 1) % 7, CHV_R, &x1, &y1);
        p3d_line(x0, y0, x1, y1, 0.0f, 1, CHV_EDGE);
    }
    if (s < 0)
        return;
    if (v->held < 0 && v->chords.len > 1) {         /* the chord before, a wire ghost */
        n = chv_degrees(v->chords.ch[(s + v->chords.len - 1) % v->chords.len], d);
        for (i = 0; i < n; i++) {
            chv_at(d[i], CHV_R, &x0, &y0), chv_at(d[(i + 1) % n], CHV_R, &x1, &y1);
            p3d_line(x0, y0, x1, y1, 0.5f, 1, CHV_GHOST);
        }
    }
    n = chv_degrees(v->chords.ch[s], d);
    float cx = 0.0f, cy = 0.0f, px[4], py[4];
    for (i = 0; i < n; i++)
        chv_at(d[i], CHV_R, &px[i], &py[i]), cx += px[i], cy += py[i];
    cx /= (float)n, cy /= (float)n;
    int hot = v->held < 0 && ms - chv.hit_ms < CHV_HIT_MS;
    const uint16_t *t = v->held >= 0 || hot ? MB_WHITE : PINK3;
    for (i = 0; i < n; i++)                         /* the chord: facets from its centre */
        p3d_tri(px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], cx, cy, 1.0f, t[i % 3]);
}

void chords_band(const q_chview *v, int32_t oy)
{
    int s = chv_shown(v), d[4] = {0}, n = 0, i;
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    if (v->kit) {
        glo_text(120, oy + 80, &FONT_S, "NO CHORDS ON A KIT", MB_WHITE[1], 0);
        return;
    }
    if (s >= 0)
        n = chv_degrees(v->chords.ch[s], d);
    p3d_draw(oy);
    for (i = 0; i < 7; i++) {                       /* the corners and their degrees */
        float x, y, lx, ly;
        int in = 0;
        for (int j = 0; j < n; j++)
            in |= d[j] == i;
        chv_at(i, CHV_R, &x, &y), chv_at(i, CHV_R + 15.0f, &lx, &ly);
        uint16_t c = in ? MB_WHITE[1] : KBLUE[2];
        cv_rect((int32_t)x - 1, oy + (int32_t)y - 3, 3, 7, c);
        cv_rect((int32_t)x - 3, oy + (int32_t)y - 1, 7, 3, c);
        glo_text((int32_t)lx, oy + (int32_t)ly, &FONT_XS, CHV_ROMAN[i], in ? PINK3[1] : CHV_UI, 0);
    }
    if (s >= 0) {                                   /* the chord's name at its centre */
        float cx = 0.0f, cy = 0.0f, x, y;
        for (i = 0; i < n; i++)
            chv_at(d[i], CHV_R, &x, &y), cx += x, cy += y;
        int white = v->held >= 0 || chv.ms - chv.hit_ms < CHV_HIT_MS;   /* on white facets, the name in black */
        glo_text((int32_t)(cx / (float)n), oy + (int32_t)(cy / (float)n), &FONT_S, v->name, white ? RGB(0, 0, 0) : C_WHITE, 0);
    }
    for (i = 0; i < 8; i++) {                       /* the progression */
        uint16_t c = i >= v->chords.len ? RGB(0x14, 0x0e, 0x30) : i == v->held ? MB_WHITE[0] : i == v->slot && v->held < 0 ? PINK3[1] : KBLUE[2];
        cv_rect(8 + 12 * i, oy + 8, 9, 6, c);
    }
    if (v->held >= 0) {                             /* the held chord's banner */
        cv_rect(124, oy + 3, 110, 12, CHV_BANNER);
        glo_text(179, oy + 9, &FONT_XS, v->banner, RGB(0, 0, 0), 0);
    } else {                                        /* RATE's notches, lit up to the strike in progress */
        int r = 1 << v->chords.rate, done = v->slot >= 0 ? v->step / (16 / r) : -1;
        for (i = 0; i < r; i++) {
            int32_t x = 170 + (int32_t)((float)(i + 0.5f) * 62.0f / (float)r);
            cv_rect(x - 1, oy + 12, 3, 3, i <= done ? PINK3[1] : CHV_EDGE);
        }
    }
    /* the keyboard from NOTE - 12: the notes struck pink, each white as it starts (STRUM one after the other) */
    int lo = v->chords.note - 12, wx;
    for (int pass = 0; pass < 2; pass++) {
        wx = 6;
        for (int m = lo; wx < 232 && m < 128; m++) {
            int pc = m % 12, black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10, k = -1;
            if (!black && pass == 1) { wx += 12; continue; }
            if (black && pass == 0) continue;
            for (int j = 0; j < v->n; j++)
                if (v->notes[j] == m) k = j;
            uint32_t at = chv.hit_ms + (uint32_t)k * v->chords.strum * v->step_ms / 24u;
            int lit = k >= 0 && v->held < 0 && chv.ms - at < CHV_HIT_MS && (int32_t)(chv.ms - at) >= 0;
            if (!black) {
                cv_rect(wx, oy + CHV_KY, 11, 22, k < 0 ? CHV_KEY : v->held >= 0 ? MB_WHITE[1] : lit ? MB_WHITE[0] : PINK3[1]);
                if (pc == 0) {
                    char b[4] = {'C', (char)('0' + (m / 12 - 1) % 10), 0, 0};
                    glo_text(wx + 1, oy + CHV_KY + 18, &FONT_XS, b, RGB(0x8a, 0x84, 0xab), 2);
                }
                wx += 12;
            } else {
                cv_rect(wx - 4, oy + CHV_KY, 8, 13, k < 0 ? CHV_BLACK : v->held >= 0 ? MB_WHITE[2] : lit ? MB_WHITE[0] : PINK3[2]);
            }
        }
    }
}

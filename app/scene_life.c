/* LIFE's molecules (spec LIFE section 6; docs/screens/life.html, direction M3): the 16 x 8 grid seen from the front,
 * a living cell an atom, bonds to its living neighbours, the next generation foretold (doomed atoms tremble dark, the
 * births to come as wire ghosts). Part of the unity build after scene_glo.c (app_ui.c): KBLUE, KGREEN, ORANGE,
 * MB_WHITE, PINK3, glo_text. */

#define LF_X0 22.0f                 /* the grid in the scene area: column c from LF_X0 + LF_CW c, row r from the bottom */
#define LF_CW 13.5f
#define LF_Y0 18.0f
#define LF_RH 18.0f
#define LF_BIRTH_MS 350u
#define LF_DEATH_MS 450u
#define LF_PLAY_MS 150u
#define LF_PHOTON_MS 300u
#define LF_ELECTRON_MS 600u
#define LF_SEED_MS 600u
static const uint16_t LF_DOOMED[3] = {RGB(0x1c, 0x35, 0x90), RGB(0x14, 0x25, 0x60), RGB(0x0c, 0x16, 0x40)};
static const uint16_t LF_GREY[3] = {RGB(0x6a, 0x66, 0x80), RGB(0x4a, 0x46, 0x60), RGB(0x2e, 0x2b, 0x40)};
static const uint16_t LF_BOND[3] = {RGB(0x6f, 0x68, 0xa8), RGB(0x4a, 0x44, 0x80), RGB(0x2c, 0x28, 0x58)};
#define LF_BG RGB(0x07, 0x04, 0x1a)
#define LF_STRIP RGB(0x13, 0x0e, 0x2e)
#define LF_UI RGB(0x8b, 0x84, 0xa8)
#define LF_DIM RGB(0x2a, 0x21, 0x48)

static struct {
    int seen;
    uint8_t gens, reseeds;
    int8_t col;
    uint32_t gen_ms, seed_ms, col_ms, ms;
} lf;

void life_enter(void) { lf.seen = 0; }

static float lf_cx(int c) { return LF_X0 + LF_CW * ((float)c + 0.5f); }
static float lf_cy(int r) { return LF_Y0 + LF_RH * ((float)(LIFE_H - 1 - r) + 0.5f); }
static int lf_alive(const uint16_t g[LIFE_H], int c, int r) { return g[(r + LIFE_H) % LIFE_H] >> ((c + LIFE_W) % LIFE_W) & 1; }

/* the rule's neighbours, on the torus */
static int lf_near(const uint16_t g[LIFE_H], int c, int r)
{
    int n = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            n += (dr || dc) && lf_alive(g, c + dc, r + dr);
    return n;
}

/* a row's tones: blue on a synth; on a KIT its pad's (PADS) or its band's (BANDS): BD orange, SN green, HH white */
static const uint16_t *lf_tones(const q_lview *v, int r)
{
    if (!v->kit)
        return KBLUE;
    int i = v->map ? (r < 3 ? 0 : r < 6 ? 1 : 2) : r;
    return i == 0 ? ORANGE : i == 1 ? KGREEN : i == 2 ? MB_WHITE : KBLUE;
}

/* a flat hexagon in 4 triangles, its facets toned by their side against a light from the top left */
static void lf_atom(float x, float y, float rad, float a, float z, const uint16_t t[3])
{
    float px[6], py[6];
    for (int k = 0; k < 6; k++) {
        float b = a + (float)k * 1.0471976f;
        px[k] = x + fm_cosf(b) * rad, py[k] = y + fm_sinf(b) * rad;
    }
    for (int k = 1; k < 5; k++) {
        float mx = (px[0] + px[k] + px[k + 1]) / 3.0f - x, my = (py[0] + py[k] + py[k + 1]) / 3.0f - y;
        float d = -0.6f * mx - 0.8f * my;
        p3d_tri(px[0], py[0], px[k], py[k], px[k + 1], py[k + 1], z, d > 0.6f ? t[0] : d > -0.6f ? t[1] : t[2]);
    }
}

static void lf_diamond(float x, float y, float s, float z, uint16_t c)
{
    p3d_tri(x, y - s, x + s, y, x, y + s, z, c);
    p3d_tri(x, y - s, x, y + s, x - s, y, z, c);
}

void life_frame(const q_lview *v, uint32_t ms)
{
    const q_life_run *l = &v->run;
    float t = (float)(ms % 3600000u) * 0.001f;
    int c, r, k;
    if (!lf.seen || l->gens != lf.gens || l->reseeds != lf.reseeds) {
        uint32_t when = lf.seen ? ms : ms - 100000u;     /* arriving: what happened before does not animate */
        if (!lf.seen || l->reseeds != lf.reseeds) lf.seed_ms = when;
        if (!lf.seen || l->gens != lf.gens) lf.gen_ms = when;
        lf.gens = l->gens, lf.reseeds = l->reseeds;
    }
    if (!lf.seen || v->col != lf.col)
        lf.col = v->col, lf.col_ms = lf.seen ? ms : ms - 100000u;
    lf.seen = 1;
    lf.ms = ms;
    uint32_t since_gen = ms - lf.gen_ms, since_seed = ms - lf.seed_ms, since_col = ms - lf.col_ms;
    uint32_t since_born = since_gen < since_seed ? since_gen : since_seed;
    int stuck = l->stuck && !v->autos;
    p3d_begin();

    /* the atoms, those of the column read first: the budget drops what comes last */
    for (int pass = 0; pass < 2; pass++)
        for (c = 0; c < LIFE_W; c++) {
            if ((c == v->col) != !pass)
                continue;
            for (r = 0; r < LIFE_H; r++) {
                int i = r * LIFE_W + c, n = lf_near(l->grid, c, r);
                float x = lf_cx(c), y = lf_cy(r), rad = 5.5f;
                const uint16_t *tn = lf_tones(v, r);
                if (!lf_alive(l->grid, c, r)) {
                    if (since_gen < LF_DEATH_MS && lf_alive(l->prev[0], c, r)) {    /* dying: shrinks, grey */
                        float k2 = (float)since_gen / (float)LF_DEATH_MS;
                        lf_atom(x, y + k2 * 8.0f, rad * (1.0f - k2), t, 2.0f, LF_GREY);
                    }
                    continue;
                }
                int played = c == v->col && (v->played >> r & 1) && since_col < LF_PLAY_MS;
                int born = (l->born[r] >> c & 1) && since_born < LF_BIRTH_MS;
                if (born) {                                         /* grows; after a reseed, drops in too */
                    float g = (float)since_born / (float)LF_BIRTH_MS;
                    rad *= g;
                    if (since_seed < LF_BIRTH_MS) {
                        float d = (float)since_seed / (float)LF_BIRTH_MS - p3d_hash((uint32_t)i + 9100u) * 0.5f;
                        d = d < 0.0f ? 0.0f : d;
                        y -= (1.0f - d) * (1.0f - d) * 40.0f;
                    }
                }
                if (played)
                    rad *= 1.35f, tn = PINK3;
                else if (born)
                    tn = PINK3;
                else if (stuck)
                    tn = LF_GREY;
                else if (n < 2 || n > 3) {                          /* doomed: trembles, dark */
                    tn = LF_DOOMED;
                    x += fm_sinf(t * 31.0f + (float)i) * 1.6f, y += fm_cosf(t * 27.0f + (float)i * 3.0f) * 1.6f;
                }
                lf_atom(x, y, rad, t * 0.6f + (float)i, 2.0f, tn);
            }
        }

    /* the bonds: to the right, up and the two diagonals above, not across the torus' seam */
    for (r = 0; r < LIFE_H; r++)
        for (c = 0; c < LIFE_W; c++) {
            static const int8_t D[4][2] = {{1, 0}, {0, 1}, {1, 1}, {-1, 1}};
            if (!lf_alive(l->grid, c, r))
                continue;
            for (k = 0; k < 4; k++) {
                int c2 = c + D[k][0], r2 = r + D[k][1];
                if (c2 < 0 || c2 >= LIFE_W || r2 >= LIFE_H || !lf_alive(l->grid, c2, r2))
                    continue;
                int lit = since_col < 200u && ((c == v->col && (v->played >> r & 1)) || (c2 == v->col && (v->played >> r2 & 1)));
                uint16_t col = lit ? PINK3[1] : stuck ? LF_GREY[2] : LF_BOND[1];
                float x0 = lf_cx(c), y0 = lf_cy(r), x1 = lf_cx(c2), y1 = lf_cy(r2);
                if (D[k][0] && D[k][1]) {
                    p3d_line(x0, y0, x1, y1, 1.0f, 2, col);
                } else {                                            /* double: two lines 1.6 px either side */
                    float ox = D[k][1] ? 1.6f : 0.0f, oy = D[k][0] ? 1.6f : 0.0f;
                    p3d_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, 1.0f, 1, col);
                    p3d_line(x0 - ox, y0 - oy, x1 - ox, y1 - oy, 1.0f, 1, col);
                }
            }
        }

    /* photons from the atoms that play, electrons round the newborns, MUT's sparks on the cells it added */
    if (v->col >= 0 && since_col < LF_PHOTON_MS)
        for (r = 0; r < LIFE_H; r++) {
            if (!(v->played >> r & 1))
                continue;
            float k2 = (float)since_col / (float)LF_PHOTON_MS;
            for (k = 0; k < 6; k++) {
                float a = (float)k * 1.0471976f + 0.3f, d = 6.0f + k2 * 12.0f;
                lf_diamond(lf_cx(v->col) + fm_cosf(a) * d, lf_cy(r) + fm_sinf(a) * d, 1.5f, 3.0f, k2 < 0.6f ? PINK3[0] : PINK3[2]);
            }
        }
    if (since_born < LF_ELECTRON_MS && !stuck)
        for (r = 0; r < LIFE_H; r++)
            for (c = 0; c < LIFE_W; c++) {
                if (!(l->born[r] >> c & 1) || !lf_alive(l->grid, c, r) || (l->mut[r] >> c & 1))
                    continue;
                for (k = 0; k < 3; k++) {
                    float a = t * 6.0f + (float)k * 2.0943951f;
                    lf_diamond(lf_cx(c) + fm_cosf(a) * 9.0f, lf_cy(r) + fm_sinf(a) * 3.5f - fm_cosf(a) * 2.0f, 1.4f, 3.0f, KGREEN[0]);
                }
            }
    if (since_gen < LF_BIRTH_MS)
        for (r = 0; r < LIFE_H; r++)
            for (c = 0; c < LIFE_W; c++)
                if (l->mut[r] >> c & 1) {
                    float y = 14.0f + (lf_cy(r) - 14.0f) * (float)since_gen / (float)LF_BIRTH_MS;
                    p3d_line(lf_cx(c), y - 8.0f, lf_cx(c), y, 3.0f, 2, PINK3[1]);
                    lf_diamond(lf_cx(c), y, 2.0f, 3.0f, PINK3[0]);
                }
    for (k = 0; k < v->mut * 10 / 127; k++) {      /* MUT: sparks falling slowly across the grid */
        float f = t * 0.25f + p3d_hash((uint32_t)k * 7u + 9300u), u = f - fm_floorf(f);
        lf_diamond(LF_X0 + p3d_hash((uint32_t)k * 13u + (uint32_t)fm_floorf(f) * 31u + 9400u) * (LF_CW * LIFE_W), LF_Y0 + u * LF_RH * LIFE_H,
                   1.2f, 0.5f, PINK3[2]);
    }

    /* the births to come: empty cells with three living neighbours, as wire diamonds */
    if (!stuck)
        for (r = 0; r < LIFE_H; r++)
            for (c = 0; c < LIFE_W; c++) {
                if (lf_alive(l->grid, c, r) || lf_near(l->grid, c, r) != 3)
                    continue;
                float x = lf_cx(c), y = lf_cy(r), s = 4.5f + fm_sinf(t * 4.0f + (float)(c + r)) * 0.6f;
                p3d_line(x, y - s, x + s, y, 0.0f, 1, KBLUE[1]);
                p3d_line(x + s, y, x, y + s, 0.0f, 1, KBLUE[1]);
                p3d_line(x, y + s, x - s, y, 0.0f, 1, KBLUE[1]);
                p3d_line(x - s, y, x, y - s, 0.0f, 1, KBLUE[1]);
            }
}

void life_band(const q_lview *v, int32_t oy)
{
    const q_life_run *l = &v->run;
    float t = (float)(lf.ms % 3600000u) * 0.001f;
    int k;
    cv_rect(0, oy, 240, 166, LF_BG);
    for (k = 0; k < 30; k++) {                      /* the culture medium: slow specks */
        float f = t * (0.004f + p3d_hash((uint32_t)k + 9500u) * 0.008f) + p3d_hash((uint32_t)k * 3u + 9600u);
        int32_t x = (int32_t)((f - fm_floorf(f)) * 240.0f), y = 16 + (int32_t)(p3d_hash((uint32_t)k * 5u + 9700u) * 148.0f);
        cv_rect(x, oy + y, 1, 1, k % 4 ? RGB(0x1a, 0x15, 0x38) : RGB(0x2c, 0x25, 0x58));
    }
    if (v->col >= 0) {
        int32_t x = (int32_t)(LF_X0 + LF_CW * (float)v->col);
        cv_rect(x, oy + (int32_t)LF_Y0 - 2, 14, (int32_t)(LF_RH * LIFE_H) + 4, LF_STRIP);
        for (k = 0; k < 4; k++) {                   /* POLY's notches over the column */
            float nx = (float)x + 0.5f + (float)k * 3.5f;
            p3d_fill(nx, (float)(oy + 17), nx + 1.5f, (float)(oy + 13), nx + 3.0f, (float)(oy + 17), k < v->poly ? PINK3[0] : LF_DIM);
        }
    }
    p3d_draw(oy);
    for (int r = 0; r < LIFE_H; r++)
        glo_text(19, oy + (int32_t)lf_cy(r), &FONT_XS, v->label[r], v->kit ? lf_tones(v, r)[1] : LF_UI, 1);
    /* the top strip: GEN's diamonds (one lit per tour gone), AUTO's arrows, the seed */
    int n = 1 << v->gen, done = n > 1 ? l->tours % n : 1;
    for (k = 0; k < n; k++) {
        float x = 120.0f + 8.0f * (float)k, y = (float)(oy + 7);
        uint16_t c = k < done ? ORANGE[1] : LF_DIM;
        p3d_fill(x, y - 3.0f, x + 3.0f, y, x, y + 3.0f, c);
        p3d_fill(x, y - 3.0f, x, y + 3.0f, x - 3.0f, y, c);
    }
    if (v->autos) {
        for (k = 0; k < 2; k++) {
            float a = t * 2.0f + (float)k * 3.1415927f, ax = 196.0f + fm_cosf(a) * 5.0f, ay = (float)(oy + 7) + fm_sinf(a) * 5.0f;
            float tx = -fm_sinf(a), ty = fm_cosf(a);
            p3d_fill(ax + tx * 3.5f, ay + ty * 3.5f, ax - ty * 2.5f, ay + tx * 2.5f, ax + ty * 2.5f, ay - tx * 2.5f, KGREEN[1]);
        }
    } else {
        cv_rect(192, oy + 3, 9, 1, LF_UI), cv_rect(192, oy + 11, 9, 1, LF_UI);
        cv_rect(192, oy + 3, 1, 9, LF_UI), cv_rect(200, oy + 3, 1, 9, LF_UI);
    }
    char s[5] = {'S', (char)('0' + v->seed / 100), (char)('0' + v->seed / 10 % 10), (char)('0' + v->seed % 10), 0};
    glo_text(232, oy + 7, &FONT_S, s, lf.ms - lf.seed_ms < LF_SEED_MS ? PINK3[0] : LF_UI, 1);
    if (l->stuck && !v->autos) {
        int empty = 1;
        for (k = 0; k < LIFE_H; k++)
            empty &= !l->grid[k];
        glo_text(128, oy + 84, &FONT_S, empty ? "EXTINCT" : "FROZEN", MB_WHITE[1], 0);
    }
}

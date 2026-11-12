/* The analog engines' scene (spec 5d1, 3; docs/screens/analog.html crests): flying over crests that carry the
 * engine's waveform, each one shifted from the last; a ship fires at the crest ahead on every note. VA VCF, PHASE
 * DIST, VA, WAVESHAPE. Part of the unity build after scene.c (app_ui.c): its sc, sb, stars, PINK3. */

#define VA_COLS 12                  /* cells across the useful width |x| <= 5.4 */
#define VA_ROWS 11                  /* rows of heights: 10 bands, as far as the mock-up */
#define VA_ZS 1.5f                  /* rows' spacing */
#define VA_Z0 1.4f
#define VA_HZ 37                    /* the horizon in the scene area */
#define VA_SHIP_Z 3.2f
static const uint16_t VA_LOW[3] = {RGB(0x6b, 0x4b, 0xc4), RGB(0x3d, 0x2a, 0x85), RGB(0x22, 0x16, 0x4f)};
static const uint16_t VA_HIGH[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
#define VA_FOG RGB(0x17, 0x0f, 0x38)
#define VA_WIRE RGB(0x8f, 0xb4, 0xff)
static struct {
    float t, scroll, heat, sx, ship_x, ship_y, bank, shot[4];
    int ship, shoot, aimed;
    uint32_t hit_seen;
} va;

/* the VA VCF's filtered wave, 64 samples: a saw-to-square source (MORPH) through a state-variable filter (TIMB the
   cut-off, HARM the resonance), its fourth pass kept, normalised. Two filter steps a sample, f = 2 sin(pi fc / 2): the
   mock-up's one step (f capped at 1.2) blew up open with no resonance (HARM 0, TIMB 127) */
static void va_vcf_table(float h, float tb, float m)
{
    float fc = 0.02f * fm_exp2f(3.6f * tb), f = 2.0f * fm_sinf(0.5f * FM_PI * fc), q = 2.0f * (1.0f - 0.85f * h);
    float lp = 0, bp = 0, mx = 1e-6f;
    for (int pass = 0; pass < 4; pass++)
        for (int i = 0; i < 64; i++) {
            float p = (float)i / 64.0f, sq = p < 0.5f ? 1.0f : -1.0f, src;
            if (m < 0.5f)
                src = (2.0f * p - 1.0f) * (1.0f - 2.0f * m) + sq * 2.0f * m;
            else
                src = p < 0.5f - (m - 0.5f) * 0.6f ? 1.0f : -1.0f;
            for (int k = 0; k < 2; k++) {
                lp += f * bp;
                float hp = src - lp - q * bp;
                bp += f * hp;
            }
            if (pass == 3) {
                sb.va.tab[i] = lp;
                mx = fm_maxf(mx, fm_fabsf(lp));
            }
        }
    for (int i = 0; i < 64; i++)
        sb.va.tab[i] /= mx;
}

static float va_osc(float s, float p)   /* VA: triangle to saw (s < 0.5), saw to narrowing pulse */
{
    float tri = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p, saw = 2.0f * p - 1.0f;
    if (s < 0.5f)
        return tri * (1.0f - 2.0f * s) + saw * 2.0f * s;
    float k = 2.0f * (s - 0.5f), pw = 0.5f - (s - 0.5f) * 0.6f;
    return saw * (1.0f - k) + (p < pw ? 1.0f : -1.0f) * k;
}

/* the model's waveform at phase p (any), -1..1; r: the row (VA's second oscillator drifts from row to row) */
static float va_wave(int model, const float k[4], float p, int r)
{
    float h = k[0], tb = k[1], m = k[2];
    p -= fm_floorf(p);
    if (model == 0) {                   /* VA VCF */
        float x = p * 64.0f;
        int i = (int)x;
        float a = sb.va.tab[i & 63], b = sb.va.tab[(i + 1) & 63];
        return a + (b - a) * (x - (float)i);
    }
    if (model == 1) {                   /* PHASE DIST */
        float pw = 0.5f + 0.35f * m, q = p < pw ? 0.5f * p / pw : 0.5f + 0.5f * (p - pw) / (1.0f - pw);
        float ratio = 1.0f + fm_floorf(3.0f * h + 0.5f);
        return fm_sinf(FM_TWO_PI * q + 1.8f * tb * fm_sinf(FM_TWO_PI * ratio * p));
    }
    if (model == 8) {                   /* VA */
        float ph2 = (float)r * 0.18f * (0.1f + 1.4f * h);
        float p2 = p + ph2 - fm_floorf(p + ph2);
        return 0.5f * (va_osc(tb, p) + va_osc(m, p2));
    }
    /* WAVESHAPE: an asymmetric triangle (MORPH), saturated (HARM), folded (TIMB) */
    float pw = 0.5f + (m - 0.5f) * 0.7f, x = p < pw ? 2.0f * p / pw - 1.0f : 1.0f - 2.0f * (p - pw) / (1.0f - pw);
    float y = (1.0f - h) * x + h * fm_tanhf(3.0f * x) / 0.99505475f;      /* tanh 3 */
    return fm_sinf(y * (1.0f + 2.2f * tb) * 0.5f * FM_PI);
}

static float va_row_z(int r) { return VA_Z0 + VA_ZS * (float)r - va.scroll; }
static float va_x(int i) { return -5.4f + 10.8f * (float)i / VA_COLS; }

/* the ground's height at x on row r: crests on even rows, valleys on odd ones, flat beyond |x| = 5.4 */
static float va_height(int model, const float k[4], float x, int r)
{
    float env = fm_clampf((5.4f - fm_fabsf(x)) / 1.6f, 0.0f, 1.0f);
    float u = 0.5f + 0.5f * va_wave(model, k, x / 3.6f + 0.25f + 0.2f * (float)r, r);
    return (r & 1) ? env * (0.04f + 0.22f * u) : env * (0.15f + 1.15f * u);
}

/* k: HARM, TIMB, MORPH (the waveform), DECAY (how long a shot lasts) */
void va_frame(const q_sview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.0f, 0.5f, 0.0f, 4.2f, 130.0f, 106.0f};
    float t = (float)(ms % 3600000u) * 0.001f, k[4];
    for (int i = 0; i < 4; i++)
        k[i] = (float)v->k[i] / 127.0f;
    if (sb_owner != SB_VA || v->k[0] != sb.va.k[0] || v->k[1] != sb.va.k[1] || v->k[2] != sb.va.k[2]) {
        sb_owner = SB_VA;               /* (the table only when a knob moved, or another scene had the room) */
        sb.va.k[0] = v->k[0], sb.va.k[1] = v->k[1], sb.va.k[2] = v->k[2];
        va_vcf_table(k[0], k[1], k[2]);
    }
    float since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    va.t = t;
    va.scroll = 2.0f * t;
    va.heat = since < 3.0f ? fm_expf(-since / (0.05f + 0.4f * k[3])) : 0.0f;
    if (sc.hits[0] != va.hit_seen) {    /* a note: the crest ahead of the ship is aimed at */
        va.hit_seen = sc.hits[0];
        int a = (int)fm_floorf((VA_SHIP_Z - VA_Z0 + va.scroll) / VA_ZS + 0.01f) + 1;
        va.aimed = a + (a & 1);         /* (crests are the even rows) */
    }
    int first = (int)fm_floorf((va.scroll + 0.2f - VA_Z0) / VA_ZS) + 1;
    float far = va_row_z(first + VA_ROWS - 1);
    for (int r = 0; r < VA_ROWS; r++) {
        float z = va_row_z(first + r), grow = fm_minf(1.0f, (far - z) / 3.0f + 0.05f);
        for (int i = 0; i <= VA_COLS; i++)
            sb.va.h[r * (VA_COLS + 1) + i] = grow * va_height(v->model, k, va_x(i), first + r);
    }
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.8f, 0.45f, 0.1f}, VA_FOG, 18.0f);
    va.sx = 1.4f * fm_sinf(0.8f * t);
    int hot = va.heat > 0.4f;
    for (int r = 0; r + 1 < VA_ROWS; r++) {     /* each band: near row r, back row r + 1 */
        float zn = va_row_z(first + r), zb = va_row_z(first + r + 1);
        const float *hn = &sb.va.h[r * (VA_COLS + 1)], *hb = hn + VA_COLS + 1;
        for (int i = 0; i < VA_COLS; i++) {
            p3_vec n0 = {va_x(i), hn[i], zn}, n1 = {va_x(i + 1), hn[i + 1], zn};
            p3_vec b0 = {va_x(i), hb[i], zb}, b1 = {va_x(i + 1), hb[i + 1], zb};
            const p3_vec t1[3] = {n0, b0, b1}, t2[3] = {n0, b1, n1};
            int aim = hot && first + r + 1 == va.aimed && fm_fabsf(0.5f * (n0.x + n1.x) - va.sx) < 0.4f;
            p3d_tri3(t1, aim ? PINK3 : (n0.y + b0.y + b1.y) < 0.9f ? VA_LOW : VA_HIGH, 0, 0.0f);
            p3d_tri3(t2, (n0.y + b1.y + n1.y) < 0.9f ? VA_LOW : VA_HIGH, 0, 0.0f);
        }
    }
    for (int r = 0; r < VA_ROWS; r++)           /* the crests' wire, every other point */
        if (!((first + r) & 1)) {
            uint16_t c = first + r == va.aimed && va.heat > 0.12f ? p3d_mix(PINK3[1], VA_WIRE, 1.0f - va.heat) : VA_WIRE;
            const float *h = &sb.va.h[r * (VA_COLS + 1)];
            for (int i = 0; i + 2 <= VA_COLS; i += 2)
                p3d_line3((p3_vec){va_x(i), h[i], va_row_z(first + r)}, (p3_vec){va_x(i + 2), h[i + 2], va_row_z(first + r)},
                          1, c, 0.05f);
        }
    /* the ship, riding the ground ahead (2D, in front: va_band) */
    float rf = (VA_SHIP_Z - VA_Z0 + va.scroll) / VA_ZS, z;
    int ra = (int)fm_floorf(rf);
    float h0 = va_height(v->model, k, va.sx, ra), h1 = va_height(v->model, k, va.sx, ra + 1);
    float gy = h0 + (h1 - h0) * (rf - (float)ra);
    va.ship = p3d_project((p3_vec){va.sx, fm_maxf(gy, h1) + 0.5f, VA_SHIP_Z}, &va.ship_x, &va.ship_y, &z);
    va.bank = 0.35f * fm_cosf(0.8f * t);
    va.shoot = hot && va.ship && p3d_project((p3_vec){va.sx, 0.8f * va_height(v->model, k, va.sx, va.aimed),
                                                      va_row_z(va.aimed) - 0.2f}, &va.shot[2], &va.shot[3], &z);
    va.shot[0] = va.ship_x, va.shot[1] = va.ship_y - 2.0f;
}

void va_band(int32_t oy)
{
    static const uint16_t SKY[4] = {RGB(0x06, 0x03, 0x1a), RGB(0x0b, 0x06, 0x24), RGB(0x12, 0x0a, 0x30), RGB(0x1b, 0x0f, 0x3d)};
    static const int SKY_Y[4] = {0, 14, 26, 29};
    float t = va.t, o = (float)oy;
    int i, k;
    for (i = 0; i < 4; i++)
        cv_rect(0, oy + SKY_Y[i], 240, VA_HZ - SKY_Y[i], SKY[i]);
    stars(oy, t, 30, 3, 20, 1.0f);
    for (i = 0; i < 8; i++) {                   /* the moon: a cut disc, lit from the top left, and a crater */
        float a0 = (float)i * (FM_TWO_PI / 8.0f) - FM_PI / 8.0f, a1 = a0 + FM_TWO_PI / 8.0f, mid = a0 + FM_PI / 8.0f;
        float d = -0.7f * fm_cosf(mid) - 0.7f * fm_sinf(mid);
        p3d_fill(184, 16 + o, 184 + 10 * fm_cosf(a0), 16 + 10 * fm_sinf(a0) + o, 184 + 10 * fm_cosf(a1), 16 + 10 * fm_sinf(a1) + o,
                 d > 0.4f ? RGB(0xe4, 0xdd, 0xff) : d > -0.4f ? RGB(0xb3, 0xa6, 0xe8) : RGB(0x6f, 0x5f, 0xb4));
    }
    p3d_fill(180, 14 + o, 185, 11 + o, 187, 17 + o, RGB(0x8a, 0x7b, 0xcc));
    p3d_fill(180, 14 + o, 187, 17 + o, 182, 19 + o, RGB(0x8a, 0x7b, 0xcc));
    for (k = 0; k < 2; k++) {                   /* two far ranges sliding left, the nearer faster */
        static const uint16_t RC[2][2] = {{RGB(0x1f, 0x14, 0x46), RGB(0x17, 0x0f, 0x38)}, {RGB(0x2a, 0x1a, 0x58), RGB(0x1f, 0x13, 0x46)}};
        float speed = k ? 4.0f : 2.0f, run = t * speed, off = run - 16.0f * fm_floorf(run / 16.0f);
        int base = (int)fm_floorf(run / 16.0f);
        for (i = 0; i < 18; i++) {
            float x0 = (float)(i * 16) - off - 8.0f, mid = x0 + 8.0f, x1 = x0 + 16.0f;
            float top = (float)VA_HZ - (k ? 2.0f : 6.0f) - (4.0f + (k ? 9.0f : 16.0f) * p3d_hash((uint32_t)(((i + base) % 19) * 2 + k + 900)));
            p3d_fill(x0, VA_HZ + o, mid, top + o, mid, VA_HZ + o, RC[k][0]);
            p3d_fill(mid, top + o, x1, VA_HZ + o, mid, VA_HZ + o, RC[k][1]);
        }
    }
    /* the flat ground, row by row: the mock-up's flat violet facets (base tone, the light against an upward normal),
       fogged with their distance; no primitive spent on them */
    for (i = VA_HZ + 1; i < 166; i++) {
        float sl = (106.0f - (float)i - 0.5f) / 130.0f, cp = 0.87758f, sp = 0.47943f;   /* pitch 0.5 */
        float z = -4.2f * (cp + sl * sp) / (sl * cp - sp), depth = z * cp + 4.2f * sp;
        cv_rect(0, oy + i, 240, 1, p3d_fogged(VA_LOW[1], depth));
    }
    cv_rect(0, oy + VA_HZ, 240, 1, RGB(0x4b, 0x2f, 0x8a));
    p3d_draw(oy);
    if (va.shoot) {                             /* the shot, 2 px */
        cv_line((int32_t)va.shot[0], oy + (int32_t)va.shot[1], (int32_t)va.shot[2], oy + (int32_t)va.shot[3], PINK3[1]);
        cv_line((int32_t)va.shot[0] + 1, oy + (int32_t)va.shot[1], (int32_t)va.shot[2] + 1, oy + (int32_t)va.shot[3], PINK3[1]);
    }
    if (va.ship) {                              /* the ship, banking as it weaves */
        float s = 1.6f, b = va.bank, px = va.ship_x, py = va.ship_y + o;
#define VP(dx, dy) px + s * (dx) - s * (dy) * b, py + s * (dy) + s * (dx) * b
        p3d_fill(VP(0, -7), VP(-13, 5), VP(0, 2), RGB(0xff, 0xff, 0xff));
        p3d_fill(VP(0, -7), VP(0, 2), VP(13, 5), RGB(0xc9, 0xc3, 0xe4));
        p3d_fill(VP(0, -4), VP(-3, 3), VP(0, 6), RGB(0x6f, 0x68, 0x90));
        p3d_fill(VP(0, -4), VP(0, 6), VP(3, 3), RGB(0x6f, 0x68, 0x90));
        float fl = 8.0f + (t * 40.0f - 3.0f * fm_floorf(t * 40.0f / 3.0f));
        p3d_fill(VP(-2, 4), VP(2, 4), VP(0, fl), va.heat > 0.4f ? PINK3[0] : PINK3[1]);
#undef VP
    }
}

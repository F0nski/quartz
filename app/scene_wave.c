/* The wavetable engines' scene (spec 5d1, 4; docs/screens/wavetable.html bowl): a rider on a hoverboard in a neon
 * bowl. WAVETABLE: a half-pipe whose walls carry the wave; WAVE TERRAIN: a bowl shaped by the terrain, ridden round
 * the circle read. Part of the unity build after scene.c (app_ui.c): its sc, sb, stars, PINK3. */

#define WV_N 8                      /* the pit's quads a side */
#define WV_TOP 1.3f                 /* the deck's height */
#define WV_E 3.6f                   /* the deck's half size */
#define WV_FOG RGB(0x0b, 0x07, 0x26)
#define WV_WIRE RGB(0x2c, 0x6f, 0xd8)
#define WV_CYAN RGB(0x5f, 0xe0, 0xff)
static const uint16_t WV_WALL[3] = {RGB(0x3d, 0x43, 0x90), RGB(0x2b, 0x30, 0x70), RGB(0x1b, 0x1f, 0x50)};
static const uint16_t WV_BLUE[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
static const uint16_t WV_GREEN[3] = {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)};
static const uint16_t WV_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const uint16_t WV_ORANGE[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)};
static const uint16_t WV_CYAN3[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xd0, 0xf0), RGB(0x1d, 0x7f, 0xa8)};
#define WV_SEGS 48                  /* the deck's tiles and the fence, drawn in the band */
static struct {
    float t, heat, k[4], sz, rx, ry;
    int terrain, flip, rider_seen;
    float since, flip_t;            /* flip_t: how far into its kickflip */
    uint32_t flip_ms, flip_hit;     /* the kickflip's start; the note that started it */
    int nseg;                       /* the deck's lines: sb.wave.seg, segc */
} wv;

static float wv_clamp01(float p) { return fm_clampf(p, 0.0f, 1.0f); }

/* WAVETABLE's wave at p in 0..1: four banks blended by HARM, TIMB their parameter, MORPH a phase warp */
static float wv_wave(float p)
{
    float h = wv.k[0], k = wv.k[1], m = wv.k[2], b[4];
    p = fm_powf(wv_clamp01(p) + 1e-6f, 1.0f + (m - 0.5f) * 1.4f);
    float n = 1.0f + 6.0f * k, s = 0.0f;
    for (int i = 1; (float)i < n + 1.0f && i <= 7; i++)
        s += fm_sinf(FM_TWO_PI * (float)i * p) / (float)i * fm_minf(1.0f, n - (float)i + 1.0f);
    b[0] = 0.7f * s;
    b[1] = (p < 0.5f - 0.42f * k ? 0.8f : -0.8f) + 0.2f * fm_sinf(FM_TWO_PI * p);
    b[2] = fm_sinf(FM_TWO_PI * p * (1.0f + 3.0f * k)) * (1.0f - p);
    b[3] = fm_sinf(fm_sinf(FM_TWO_PI * p) * (1.0f + 3.5f * k));
    float x = h * 2.999f;
    int i = (int)x;
    return b[i] + (b[i < 3 ? i + 1 : 3] - b[i]) * (x - (float)i);
}

/* WAVE TERRAIN's terrain at (u, w): four blended by HARM */
static float wv_terrain(float u, float w)
{
    float r2 = u * u + w * w, t[4];
    t[0] = fm_sinf(FM_PI * u) * fm_cosf(FM_PI * w);
    t[1] = fm_sinf(FM_PI * (1.5f * u + fm_sinf(FM_PI * w)));
    t[2] = fm_sinf(4.0f * r2) * (1.0f - 0.4f * r2);
    t[3] = fm_sinf(FM_TWO_PI * u * w) + 0.3f * fm_cosf(3.0f * FM_PI * w);
    float x = wv.k[0] * 2.999f;
    int i = (int)x;
    return fm_clampf(t[i] + (t[i < 3 ? i + 1 : 3] - t[i]) * (x - (float)i), -1.0f, 1.0f);
}

static float wv_pow6(float a) { a = a * a * a; return a * a; }

/* the pit's height at (u, w) in -1..1 */
static float wv_height(float u, float w)
{
    if (!wv.terrain)
        return 1.3f * u * u + 0.3f * wv_wave((u + 1.0f) * 0.5f) * (0.3f + 0.7f * fm_fabsf(u)) * (1.0f - wv_pow6(fm_fabsf(u)));
    return 1.3f * fm_minf(1.0f, 1.1f * (u * u + w * w)) +
           0.22f * wv_terrain(u, w) * (1.0f - wv_pow6(fm_maxf(fm_fabsf(u), fm_fabsf(w))));
}

static p3_vec wv_point(float u, float w) { return (p3_vec){u * 2.0f, wv_height(u, w), w * wv.sz}; }

/* the rider's path at time s: kick turns across the half-pipe, or round the circle read */
static void wv_path(float s, float *u, float *w)
{
    if (!wv.terrain) {
        *u = 0.88f * fm_sinf(2.1f * s);
        *w = 0.35f * fm_sinf(0.45f * s);
    } else {
        float r = 0.15f + 0.7f * wv.k[1], o = (1.9f * wv.k[2] - 1.0f) * 0.3f, a = 1.7f * s;
        *u = o + r * fm_cosf(a);
        *w = 0.5f * o + r * fm_sinf(a);
    }
}

/* a quad turned to face dir (world), then given to p3d (culled if it faces away from the camera after all) */
static void wv_quad(p3_vec a, p3_vec b, p3_vec c, p3_vec d, p3_vec dir, const uint16_t t[3], const uint16_t *edge, float bias)
{
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, wx = c.x - a.x, wy = c.y - a.y, wz = c.z - a.z;
    float n = (uy * wz - uz * wy) * dir.x + (uz * wx - ux * wz) * dir.y + (ux * wy - uy * wx) * dir.z;
    const p3_vec q[4] = {a, b, c, d}, r[4] = {a, d, c, b};
    p3d_quad3(n >= 0.0f ? q : r, t, edge, bias);
}

static p3_vec v3(float x, float y, float z) { return (p3_vec){x, y, z}; }
static p3_vec v3add(p3_vec a, p3_vec b, float k) { return (p3_vec){a.x + b.x * k, a.y + b.y * k, a.z + b.z * k}; }
static p3_vec v3unit(p3_vec a)
{
    float l = fm_sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 0.0f ? (p3_vec){a.x / l, a.y / l, a.z / l} : (p3_vec){0, 1, 0};
}

/* the rider's frame: world = pos + 1.7 (x fs + y up + z side) */
static struct { p3_vec pos, fs, up, side; } rf;
static p3_vec wv_local(float x, float y, float z)
{
    p3_vec p = v3add(rf.pos, rf.fs, 1.7f * x);
    p = v3add(p, rf.up, 1.7f * y);
    return v3add(p, rf.side, 1.7f * z);
}

/* a box of the rider's: centre and half sizes in its frame; flip: the board's turn about its length */
static void wv_box(float cx, float cy, float cz, float hx, float hy, float hz, const uint16_t t[3], float flip, float lift)
{
    p3_vec v[8];
    float cf = fm_cosf(flip), sf = fm_sinf(flip);
    for (int i = 0; i < 8; i++) {
        float y = cy + (i & 2 ? hy : -hy), z = cz + (i & 4 ? hz : -hz);
        v[i] = wv_local(cx + (i & 1 ? hx : -hx), y * cf - z * sf + lift, y * sf + z * cf);
    }
    p3d_corners3(v, t, 0);
}

static void wv_limb(p3_vec a, p3_vec b, float w, const uint16_t t[3])
{
    p3d_beam3(wv_local(a.x, a.y, a.z), wv_local(b.x, b.y, b.z), 1.7f * w, t, 0);
}

static void wv_seg(p3_vec a, p3_vec b, uint16_t c)     /* a backdrop line, projected now, drawn in the band */
{
    float xa, ya, za, xb, yb, zb;
    if (wv.nseg == WV_SEGS || !p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    int16_t *s = sb.wave.seg[wv.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.wave.segc[wv.nseg++] = p3d_fogged(c, 0.5f * (za + zb));
}

/* k: HARM, TIMB, MORPH (the wave or the terrain, the circle read), DECAY (the trail) */
void wave_frame(const q_sview *v, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f;
    p3_cam cam = {0.3f + 0.12f * fm_sinf(0.2f * t), 0.85f, 4.6f, 5.0f, 215.0f, 120.0f};
    for (int i = 0; i < 4; i++)
        wv.k[i] = (float)v->k[i] / 127.0f;
    wv.t = t;
    wv.terrain = v->model == 5;
    wv.sz = wv.terrain ? 2.0f : 1.4f;
    wv.since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    wv.heat = wv.since < 3.0f ? fm_expf(-wv.since / (0.05f + 0.4f * wv.k[3])) : 0.0f;
    if (sc.fresh)
        wv.flip_ms = ms - 100000u;
    if (!(sc.hits[0] & 1u) && sc.hit_ms[0] != wv.flip_hit && ms - wv.flip_ms >= 200u) {   /* every other note, when */
        wv.flip_ms = sc.hit_ms[0];      /* none is under way: a kickflip, played to its end through later notes */
        wv.flip_hit = sc.hit_ms[0];
    }
    wv.flip_t = (float)(ms - wv.flip_ms) * 0.001f;
    wv.flip = wv.flip_t < 0.2f;
    if (sb_owner != SB_WAVE)
        sb_owner = SB_WAVE;
    for (int j = 0; j <= WV_N; j++)          /* the pit's heights */
        for (int i = 0; i <= WV_N; i++)
            sb.wave.h[j * (WV_N + 1) + i] = wv_height(-1.0f + 2.0f * (float)i / WV_N, -1.0f + 2.0f * (float)j / WV_N);
    p3d_begin();
    p3d_camera(&cam, (p3_vec){-0.5f, 0.8f, -0.3f}, WV_FOG, 16.0f);
    const p3_vec UP = {0, 1, 0};
    for (int j = 0; j < WV_N; j++)           /* the pit, a tone a quad: lower ones darker */
        for (int i = 0; i < WV_N; i++) {
            float u0 = -1.0f + 2.0f * (float)i / WV_N, u1 = u0 + 2.0f / WV_N, w0 = -1.0f + 2.0f * (float)j / WV_N, w1 = w0 + 2.0f / WV_N;
            const float *h = &sb.wave.h[j * (WV_N + 1) + i];
            p3_vec a = {2.0f * u0, h[0], wv.sz * w0}, b = {2.0f * u1, h[1], wv.sz * w0};
            p3_vec c = {2.0f * u1, h[WV_N + 2], wv.sz * w1}, d = {2.0f * u0, h[WV_N + 1], wv.sz * w1};
            uint16_t tone = (a.y + b.y + c.y + d.y) < 1.4f ? RGB(0x1b, 0x1f, 0x50) : RGB(0x23, 0x28, 0x63);
            const uint16_t t3[3] = {tone, tone, tone}, wire = WV_WIRE;
            wv_quad(a, d, c, b, UP, t3, &wire, 0.0f);
        }
    for (int s = 0; s < 2; s++)              /* the walls up to the deck, where the pit's edge is below it */
        for (int i = 0; i < WV_N; i++) {
            float w = s ? 1.0f : -1.0f, u0 = -1.0f + 2.0f * (float)i / WV_N, u1 = u0 + 2.0f / WV_N;
            p3_vec a = wv_point(u0, w), b = wv_point(u1, w);
            if (a.y > WV_TOP - 0.02f && b.y > WV_TOP - 0.02f)
                continue;
            const uint16_t wire = WV_WIRE;
            wv_quad(a, b, v3(b.x, WV_TOP, b.z), v3(a.x, WV_TOP, a.z), v3(0, 0, -w), WV_WALL, &wire, 0.0f);
        }
    {                                        /* the deck round the pit, and its cyan coping */
        static const uint16_t DECK[3] = {RGB(0x14, 0x0f, 0x30), RGB(0x14, 0x0f, 0x30), RGB(0x14, 0x0f, 0x30)};
        float x = 2.0f, z = wv.sz, y = WV_TOP;
        wv_quad(v3(-WV_E, y, z), v3(WV_E, y, z), v3(WV_E, y, WV_E), v3(-WV_E, y, WV_E), UP, DECK, 0, 0.0f);
        wv_quad(v3(-WV_E, y, -WV_E), v3(WV_E, y, -WV_E), v3(WV_E, y, -z), v3(-WV_E, y, -z), UP, DECK, 0, 0.0f);
        wv_quad(v3(-WV_E, y, -z), v3(-x, y, -z), v3(-x, y, z), v3(-WV_E, y, z), UP, DECK, 0, 0.0f);
        wv_quad(v3(x, y, -z), v3(WV_E, y, -z), v3(WV_E, y, z), v3(x, y, z), UP, DECK, 0, 0.0f);
        y += 0.01f;
        p3d_line3(v3(-x, y, -z), v3(x, y, -z), 2, WV_CYAN, 0.1f);
        p3d_line3(v3(x, y, -z), v3(x, y, z), 2, WV_CYAN, 0.1f);
        p3d_line3(v3(x, y, z), v3(-x, y, z), 2, WV_CYAN, 0.1f);
        p3d_line3(v3(-x, y, z), v3(-x, y, -z), 2, WV_CYAN, 0.1f);
    }
    for (int s = -1; s <= 1; s += 2) {       /* two pylons, their light bars */
        p3d_line3(v3(2.7f * (float)s, WV_TOP, 2.5f), v3(2.7f * (float)s, 3.9f, 2.5f), 1, RGB(0x7a, 0x5c, 0xff), 0.0f);
        p3d_box3(v3(2.7f * (float)s, 4.05f, 2.5f), v3(0.25f, 0.06f, 0.08f), WV_CYAN3, 0);
    }
    wv.nseg = 0;                             /* the deck's tiles (cut over the pit) and the fence, in the band */
    for (float q = -WV_E; q <= WV_E + 0.01f; q += 0.9f) {
        uint16_t c = RGB(0x3b, 0x2a, 0x90);
        if (fm_fabsf(q) < 2.0f) {
            wv_seg(v3(q, WV_TOP, -WV_E), v3(q, WV_TOP, -wv.sz), c);
            wv_seg(v3(q, WV_TOP, wv.sz), v3(q, WV_TOP, WV_E), c);
        } else
            wv_seg(v3(q, WV_TOP, -WV_E), v3(q, WV_TOP, WV_E), c);
        if (fm_fabsf(q) < wv.sz) {
            wv_seg(v3(-WV_E, WV_TOP, q), v3(-2.0f, WV_TOP, q), c);
            wv_seg(v3(2.0f, WV_TOP, q), v3(WV_E, WV_TOP, q), c);
        } else
            wv_seg(v3(-WV_E, WV_TOP, q), v3(WV_E, WV_TOP, q), c);
    }
    for (float x = -WV_E; x < WV_E - 0.3f; x += 0.6f)
        wv_seg(v3(x, WV_TOP, 3.4f), v3(x, 2.0f, 3.4f), RGB(0x5a, 0x3f, 0xc0));
    wv_seg(v3(-WV_E, 2.0f, 3.4f), v3(WV_E, 2.0f, 3.4f), RGB(0x9a, 0x7c, 0xff));
    /* the painted lines: the half-pipe's profiles, or the circle read in dashes */
    if (!wv.terrain)
        for (int p = -1; p <= 1; p++)
            for (int i = 0; i < 8; i++) {
                float a0 = -1.0f + 0.25f * (float)i, a1 = a0 + 0.25f, zz = 0.6f * (float)p;
                p3_vec pa = wv_point(a0, zz), pb = wv_point(a1, zz);
                pa.y += 0.015f, pb.y += 0.015f;
                p3d_line3(pa, pb, 1, RGB(0x8f, 0xe8, 0xff), 0.05f);
            }
    else
        for (int i = 0; i < 20; i += 2) {
            float r = 0.15f + 0.7f * wv.k[1], o = (1.9f * wv.k[2] - 1.0f) * 0.3f;
            float a0 = (float)i * (FM_TWO_PI / 20.0f), a1 = a0 + FM_TWO_PI / 20.0f;
            p3_vec pa = wv_point(o + r * fm_cosf(a0), 0.5f * o + r * fm_sinf(a0)), pb = wv_point(o + r * fm_cosf(a1), 0.5f * o + r * fm_sinf(a1));
            pa.y += 0.015f, pb.y += 0.015f;
            p3d_line3(pa, pb, 2, RGB(0xff, 0xc1, 0x96), 0.05f);
        }
    /* the rider: placed on the surface, standing on its normal */
    float u, w, u2, w2, e = 0.02f;
    wv_path(t, &u, &w);
    wv_path(t + 0.02f, &u2, &w2);
    rf.pos = wv_point(u, w);
    rf.up = v3unit(v3(-(wv_height(u + e, w) - wv_height(u - e, w)) / (2.0f * e * 2.0f), 1.0f,
                      -(wv_height(u, w + e) - wv_height(u, w - e)) / (2.0f * e * wv.sz)));
    p3_vec fw;
    if (!wv.terrain) {
        float dx = fm_tanhf(5.0f * fm_cosf(2.1f * t));
        fw = v3(dx, 0.0f, fm_sqrtf(fm_maxf(0.0f, 1.0f - dx * dx)));
    } else
        fw = v3unit(v3((u2 - u) * 2.0f, 0.0f, (w2 - w) * wv.sz));
    float lift = 0.0f, flip = 0.0f;
    if (wv.flip) {                           /* in the air: upright, the board turning a full turn */
        lift = 0.12f * fm_sinf(FM_PI * wv.flip_t / 0.2f);
        flip = FM_TWO_PI * wv.flip_t / 0.2f;
        rf.up = UP;
    }
    float dn = fw.x * rf.up.x + fw.y * rf.up.y + fw.z * rf.up.z;
    rf.fs = v3unit(v3add(fw, rf.up, -dn));
    rf.side = v3(rf.up.y * rf.fs.z - rf.up.z * rf.fs.y, rf.up.z * rf.fs.x - rf.up.x * rf.fs.z, rf.up.x * rf.fs.y - rf.up.y * rf.fs.x);
    {                                        /* its shadow first, flat on the surface */
        static const uint16_t SH[3] = {RGB(0x3b, 0x35, 0x66), RGB(0x3b, 0x35, 0x66), RGB(0x3b, 0x35, 0x66)};
        p3_vec c = v3(rf.pos.x, rf.pos.y + 0.02f, rf.pos.z), f = v3(rf.fs.x, 0, rf.fs.z), sd = v3(rf.side.x, 0, rf.side.z);
        wv_quad(v3add(c, f, 0.32f), v3add(c, sd, 0.13f), v3add(c, f, -0.32f), v3add(c, sd, -0.13f), UP, SH, 0, 0.1f);
    }
    rf.pos.y += lift;
    float crouch = wv.flip ? 0.2f : fm_maxf(0.0f, 1.0f - rf.pos.y), arms = wv.flip ? 1.0f : 0.3f + 0.2f * fm_sinf(3.0f * t);
    p3_vec hip = v3(0, 0.37f - 0.12f * crouch, 0), neck = v3(0.04f + 0.04f * crouch, 0.6f - 0.14f * crouch, 0);
    wv_box(0, 0, 0, 0.2f, 0.018f, 0.07f, WV_ORANGE, flip, flip != 0.0f ? 0.09f : 0.0f);
    wv_box(0, -0.025f, 0, 0.16f, 0.008f, 0.05f, WV_CYAN3, flip, flip != 0.0f ? 0.09f : 0.0f);
    wv_limb(v3(-0.09f, 0.11f, 0.02f), hip, 0.035f, WV_BLUE);
    wv_limb(v3(0.09f, 0.11f, -0.02f), hip, 0.035f, WV_BLUE);
    wv_limb(hip, neck, 0.055f, WV_GREEN);
    wv_limb(v3(neck.x, neck.y - 0.03f, 0), v3(0.08f * arms, neck.y - 0.12f + 0.16f * arms, 0.21f), 0.025f, WV_GREEN);
    wv_limb(v3(neck.x, neck.y - 0.03f, 0), v3(-0.06f * arms, neck.y - 0.1f + 0.18f * arms, -0.21f), 0.025f, WV_GREEN);
    wv_box(neck.x + 0.01f, neck.y + 0.08f, 0, 0.055f, 0.055f, 0.055f, WV_WHITE, 0, 0);
    wv_box(neck.x + 0.01f, neck.y + 0.14f, 0, 0.07f, 0.025f, 0.07f, WV_ORANGE, 0, 0);
    wv_box(neck.x + 0.06f, neck.y + 0.09f, 0, 0.012f, 0.02f, 0.05f, WV_CYAN3, 0, 0);
    if (wv.heat > 0.08f)                     /* the trail, pink after a note, paling over DECAY */
        for (int i = 0; i < 10; i++) {
            float s0 = t - (float)i * 0.04f * (0.4f + wv.heat), s1 = s0 - 0.04f * (0.4f + wv.heat), a0, b0, a1, b1;
            wv_path(s0, &a0, &b0);
            wv_path(s1, &a1, &b1);
            p3_vec pa = wv_point(a0, b0), pb = wv_point(a1, b1);
            pa.y += 0.04f, pb.y += 0.04f;
            p3d_line3(pa, pb, 2, p3d_mix(PINK3[1], RGB(0x9a, 0x92, 0xc4), 1.0f - wv.heat), 0.15f);
        }
    float z;
    wv.rider_seen = p3d_project(rf.pos, &wv.rx, &wv.ry, &z);
}

void wave_band(int32_t oy)
{
    static const uint16_t SKY[3] = {RGB(0x06, 0x04, 0x1a), RGB(0x0b, 0x07, 0x26), RGB(0x12, 0x0b, 0x33)};
    float t = wv.t;
    int i, k;
    for (i = 0; i < 3; i++)
        cv_rect(0, oy + 30 * i, 240, 166 - 30 * i, SKY[i]);
    stars(oy, t, 30, 3, 47, 1.0f);
    for (i = 0; i < 12; i++) {               /* the city, still, its windows lit */
        int32_t x = 21 * i - 4, h = 14 + (int32_t)(26.0f * p3d_hash((uint32_t)i * 7u + 2100u)), top = 62 - h;
        cv_rect(x, oy + top, 19, h + 20, i & 1 ? RGB(0x12, 0x0b, 0x30) : RGB(0x16, 0x0e, 0x38));
        cv_rect(x, oy + top, 19, 1, RGB(0x3a, 0x2a, 0x8a));
        for (k = 0; k < 4; k++)
            if (p3d_hash((uint32_t)(i * 4 + k) + 3000u) > 0.5f)
                cv_rect(x + 3 + (k % 2) * 9, oy + top + 4 + (k / 2) * 7, 3, 2, k & 1 ? WV_CYAN : RGB(0x9a, 0x7c, 0xff));
    }
    for (i = 0; i < wv.nseg; i++)
        cv_line(sb.wave.seg[i][0], oy + sb.wave.seg[i][1], sb.wave.seg[i][2], oy + sb.wave.seg[i][3], sb.wave.segc[i]);
    p3d_draw(oy);
    if (wv.rider_seen && wv.flip_t > 0.2f && wv.flip_t < 0.55f) {   /* sparks on landing */
        float s = wv.flip_t - 0.2f;
        for (i = 0; i < 4; i++) {
            float a = -FM_PI * (0.15f + 0.7f * p3d_hash((uint32_t)i * 5u + sc.hits[0] * 17u)), sp = 60.0f + 70.0f * p3d_hash((uint32_t)i * 11u + sc.hits[0] * 3u);
            float px = wv.rx + fm_cosf(a) * sp * s, py = wv.ry + fm_sinf(a) * sp * s + 160.0f * s * s + (float)oy;
            p3d_fill(px, py - 2.5f, px + 2.5f, py + 1.5f, px - 2.0f, py + 2.0f, i & 1 ? PINK3[0] : PINK3[1]);
        }
    }
}

/* The spectral engines' scene (spec 5d1, 5; docs/screens/spectral.html garden): a garden of crystal flowers under a
 * station's neon dome, a flower per harmonic, as tall and as open as the spectrum. ADDITIVE, GRAIN FORM. Part of the
 * unity build after scene.c (app_ui.c): its sc, stars, PINK3; wv_seg's way of drawing lines in the band. */

#define GD_N 16                     /* flowers */
#define GD_FOG RGB(0x12, 0x0a, 0x2c)
#define GD_SEGS 96
static const uint16_t GD_BLOOM[3][3] = {{RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xd0, 0xf0), RGB(0x1d, 0x7f, 0xa8)},
                                        {RGB(0xe4, 0xdd, 0xff), RGB(0xb3, 0xa6, 0xe8), RGB(0x6f, 0x5f, 0xb4)},
                                        {RGB(0xc8, 0xd8, 0xff), RGB(0x7d, 0x9c, 0xf0), RGB(0x3d, 0x55, 0xa8)}};
static const uint16_t GD_STEM[3] = {RGB(0x5a, 0xd6, 0x9a), RGB(0x22, 0xa0, 0x6a), RGB(0x0f, 0x5c, 0x3c)};
static const uint16_t GD_HEART[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e)};
static struct {
    float t, since;
    int nseg, deck;                 /* the dome's lines: sb.garden.seg, segc; where the pollen rises: sb.garden.pollen */
    float deck_xy[8];
} gd;

static float gd_g(float x, float c, float w) { float d = (x - c) / w; return fm_expf(-d * d); }

/* the spectrum at x in 0..1 (a flower's place), 0.06..1 */
static float gd_spectrum(int model, const float k[4], float x)
{
    float g = 0.0f;
    if (model == 12) {                  /* ADDITIVE: 1 to 4 bumps (HARM) round the peak (TIMB), their width (MORPH) */
        int nb = 1 + (int)(3.0f * k[0] + 0.5f);
        float w = 0.05f + 0.18f * k[2];
        for (int j = 0; j < nb; j++)
            g = fm_maxf(g, gd_g(x, 0.1f + 0.8f * k[1] + ((float)j - 0.5f * (float)(nb - 1)) * 0.22f, w));
    } else {                            /* GRAIN FORM: two formants, f1 (TIMB), the second's distance (HARM), width (MORPH) */
        float w = 0.05f + 0.15f * k[2], f1 = 0.1f + 0.6f * k[1], f2 = fm_minf(0.95f, f1 + 0.1f + 0.5f * k[0]);
        g = fm_maxf(gd_g(x, f1, w), 0.8f * gd_g(x, f2, w));
    }
    return 0.06f + 0.94f * g;
}

/* an open face, seen from either side: both windings offered, culling keeps the one facing the camera */
static void gd_face(const p3_vec *p, int n, const uint16_t t[3], const uint16_t *edge, float bias)
{
    p3_vec r[4];
    for (int i = 0; i < n; i++)
        r[i] = p[n - 1 - i];
    if (n == 3) {
        p3d_tri3(p, t, edge, bias);
        p3d_tri3(r, t, edge, bias);
    } else {
        p3d_quad3(p, t, edge, bias);
        p3d_quad3(r, t, edge, bias);
    }
}

static void gd_seg(p3_vec a, p3_vec b, uint16_t c)
{
    float xa, ya, za, xb, yb, zb;
    if (gd.nseg == GD_SEGS || !p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    int16_t *s = sb.garden.seg[gd.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.garden.segc[gd.nseg++] = p3d_fogged(c, 0.5f * (za + zb));
}

/* k: HARM, TIMB, MORPH (the spectrum), DECAY (how long the hearts glow) */
void garden_frame(const q_sview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.12f, 0.26f, 4.0f, 1.75f, 200.0f, 120.0f};
    float t = (float)(ms % 3600000u) * 0.001f, k[4], z;
    for (int i = 0; i < 4; i++)
        k[i] = (float)v->k[i] / 127.0f;
    gd.t = t;
    gd.since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    float heat = gd.since < 3.0f ? fm_expf(-gd.since / (0.05f + 0.4f * k[3])) : 0.0f;
    int hot = heat > 0.4f;
    sb_owner = SB_GARDEN;
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.5f, 0.75f, -0.45f}, GD_FOG, 12.0f);
    gd.nseg = 0;                        /* the dome's arches, its back wall and ridge, the deck's grid: in the band */
    for (int a = 0; a < 7; a++)
        for (int q = 0; q < 8; q++) {
            float x = -3.0f + (float)a, a0 = FM_PI * (float)q / 8.0f, a1 = a0 + FM_PI / 8.0f;
            gd_seg((p3_vec){x, 2.9f * fm_sinf(a0), 0.15f + 1.4f * fm_cosf(a0)}, (p3_vec){x, 2.9f * fm_sinf(a1), 0.15f + 1.4f * fm_cosf(a1)},
                   RGB(0x4a, 0x36, 0xb0));
        }
    gd_seg((p3_vec){-3, 1.0f, 1.55f}, (p3_vec){3, 1.0f, 1.55f}, RGB(0x35, 0x2a, 0x8a));
    gd_seg((p3_vec){-3, 2.0f, 1.55f}, (p3_vec){3, 2.0f, 1.55f}, RGB(0x35, 0x2a, 0x8a));
    gd_seg((p3_vec){-3, 2.9f, 0.15f}, (p3_vec){3, 2.9f, 0.15f}, RGB(0x6a, 0x4e, 0xe0));
    for (int i = 0; i < 18; i++)
        gd_seg((p3_vec){-3.4f + 0.4f * (float)i, 0.001f, -1.6f}, (p3_vec){-3.4f + 0.4f * (float)i, 0.001f, 1.8f}, RGB(0x2a, 0x1f, 0x78));
    for (int i = 0; i < 9; i++)
        gd_seg((p3_vec){-3.4f, 0.001f, -1.6f + 0.4f * (float)i}, (p3_vec){3.4f, 0.001f, -1.6f + 0.4f * (float)i}, RGB(0x2a, 0x1f, 0x78));
    {                                   /* the deck under it all, in the band */
        static const p3_vec D[4] = {{-3.4f, 0, -1.6f}, {3.4f, 0, -1.6f}, {3.4f, 0, 1.8f}, {-3.4f, 0, 1.8f}};
        gd.deck = 1;
        for (int i = 0; i < 4; i++)
            gd.deck &= p3d_project(D[i], &gd.deck_xy[2 * i], &gd.deck_xy[2 * i + 1], &z);
    }
    static const uint16_t PLANTER[3] = {RGB(0x3d, 0x43, 0x90), RGB(0x2b, 0x30, 0x70), RGB(0x1b, 0x1f, 0x50)};
    const uint16_t pwire = RGB(0x3f, 0xa8, 0xff);
    p3d_box3((p3_vec){0, 0.1f, 0}, (p3_vec){2.55f, 0.1f, 0.32f}, PLANTER, &pwire);
    static const uint16_t SHARD[3] = {RGB(0x9f, 0xf0, 0xff), RGB(0x3f, 0xb8, 0xe0), RGB(0x1d, 0x6f, 0x98)};
    for (int s = 0; s < 11; s++) {      /* crystal shards along the planter */
        float gx = -2.45f + 0.466f * (float)s, gz = (p3d_hash((uint32_t)s + 800u) - 0.5f) * 0.4f, gh = 0.06f + 0.07f * p3d_hash((uint32_t)s + 900u);
        const p3_vec tri[3] = {{gx - 0.04f, 0.2f, gz}, {gx + 0.01f, 0.2f + gh, gz}, {gx + 0.04f, 0.2f, gz}};
        gd_face(tri, 3, SHARD, 0, 0.0f);
    }
    for (int i = 0; i < GD_N; i++) {
        float a = gd_spectrum(v->model, k, (float)i / 15.0f), x = 1.02f * (-2.25f + 4.5f * (float)i / 15.0f), H = 0.25f + 1.45f * a;
        float sway = 0.05f * fm_sinf(1.3f * t + 0.7f * (float)i) * H;
        p3_vec top = {x + sway, 0.2f + H, 0};
        p3d_line3((p3_vec){x, 0.2f, 0}, (p3_vec){x + 0.4f * sway, 0.2f + 0.5f * H, 0}, 2, GD_STEM[1], 0.0f);
        p3d_line3((p3_vec){x + 0.4f * sway, 0.2f + 0.5f * H, 0}, top, 2, GD_STEM[1], 0.0f);
        for (int l = 0; l < 2; l++) {   /* two leaves */
            float q = l ? 0.62f : 0.35f, sd = l ? -1.0f : 1.0f, L = 0.14f + 0.06f * a;
            p3_vec b = {x + sway * q, 0.2f + H * q, 0};
            const p3_vec tri[3] = {b, {b.x + sd * L * 0.5f, b.y + 0.06f, -0.05f}, {b.x + sd * L, b.y + 0.02f, 0}};
            gd_face(tri, 3, GD_STEM, 0, 0.0f);
        }
        float s = 0.1f + 0.2f * a, open = fm_minf(1.0f, fm_maxf(0.0f, (a - 0.25f) / 0.45f) + (hot ? 0.4f : 0.0f));
        float r = s * (0.45f + 0.75f * open), up = 1.1f * s * (1.0f - open);
        const uint16_t *bloom = GD_BLOOM[i % 3];
        for (int p = 0; p < 6; p++) {   /* six petals, a wedge each, on a plane leaning to the camera */
            float ang = (float)p * (FM_TWO_PI / 6.0f) + 0.25f * t, an[2] = {ang - 0.3f, ang + 0.3f};
            p3_vec tri[3] = {top, top, top};
            for (int e = 0; e < 2; e++) {
                float c = fm_cosf(an[e]), sn = fm_sinf(an[e]);
                tri[1 + e] = (p3_vec){top.x + c * r, top.y + 0.8f * sn * r + 0.6f * up, top.z + 0.6f * sn * r - 0.8f * up};
            }
            gd_face(tri, 3, bloom, &bloom[0], 0.0f);
        }
        {                               /* its heart, pink on a note when open */
            float hx = top.x, hy = top.y + 0.02f, hz = -0.055f, hs = 0.035f;
            const p3_vec q[4] = {{hx - hs, hy - hs, hz}, {hx + hs, hy - hs, hz}, {hx + hs, hy + hs, hz}, {hx - hs, hy + hs, hz}};
            gd_face(q, 4, hot && a > 0.35f ? PINK3 : GD_HEART, 0, 0.1f);
        }
        if (a > 0.45f) {                /* its glow on the deck */
            static const uint16_t REF[3] = {RGB(0x50, 0x74, 0x7c), RGB(0x75, 0x61, 0x79), RGB(0x5d, 0x5c, 0x7c)};
            const uint16_t c3[3] = {REF[i % 3], REF[i % 3], REF[i % 3]};
            const p3_vec q[4] = {{x - 0.16f, 0.205f, 0}, {x, 0.205f, -0.2f}, {x + 0.16f, 0.205f, 0}, {x, 0.205f, 0.2f}};
            gd_face(q, 4, c3, 0, -0.3f);
        }
        float px, py;                   /* the pollen's start, 0.2 under the flower's top */
        sb.garden.pollen[i][0] = -1;
        if (a >= 0.4f && p3d_project((p3_vec){x, 0.25f + 1.45f * a, 0}, &px, &py, &z))
            sb.garden.pollen[i][0] = (int16_t)px, sb.garden.pollen[i][1] = (int16_t)py;
    }
}

void garden_band(int32_t oy)
{
    static const uint16_t SKY[3] = {RGB(0x05, 0x03, 0x1a), RGB(0x0b, 0x06, 0x24), RGB(0x12, 0x0a, 0x2c)};
    static const int SKY_Y[3] = {0, 40, 90};
    float t = gd.t, o = (float)oy;
    int i, k;
    for (i = 0; i < 3; i++)
        cv_rect(0, oy + SKY_Y[i], 240, 166 - SKY_Y[i], SKY[i]);
    stars(oy, t, 30, 3, 56, 1.0f);
    for (i = 0; i < 8; i++) {           /* the moon, half under the screen's top */
        float a0 = (float)i * (FM_TWO_PI / 8.0f) - FM_PI / 8.0f, a1 = a0 + FM_TWO_PI / 8.0f, mid = a0 + FM_PI / 8.0f;
        float d = -0.7f * fm_cosf(mid) - 0.7f * fm_sinf(mid);
        p3d_fill(196, -4 + o, 196 + 9 * fm_cosf(a0), -4 + 9 * fm_sinf(a0) + o, 196 + 9 * fm_cosf(a1), -4 + 9 * fm_sinf(a1) + o,
                 d > 0.4f ? RGB(0xe4, 0xdd, 0xff) : d > -0.4f ? RGB(0xb3, 0xa6, 0xe8) : RGB(0x6f, 0x5f, 0xb4));
    }
    if (gd.deck) {
        const float *d = gd.deck_xy;
        p3d_fill(d[0], d[1] + o, d[2], d[3] + o, d[4], d[5] + o, RGB(0x0e, 0x0a, 0x26));
        p3d_fill(d[0], d[1] + o, d[4], d[5] + o, d[6], d[7] + o, RGB(0x0e, 0x0a, 0x26));
    }
    for (i = 0; i < gd.nseg; i++)
        cv_line(sb.garden.seg[i][0], oy + sb.garden.seg[i][1], sb.garden.seg[i][2], oy + sb.garden.seg[i][3], sb.garden.segc[i]);
    p3d_draw(oy);
    if (gd.since < 0.5f)                /* a note: pollen rises from the open flowers */
        for (i = 0; i < GD_N; i++)
            if (sb.garden.pollen[i][0] >= 0)
                for (k = 0; k < 3; k++) {
                    float px = (float)sb.garden.pollen[i][0] + (p3d_hash((uint32_t)(3 * i + k) + sc.hits[0] * 64u) - 0.5f) * 20.0f;
                    float py = (float)sb.garden.pollen[i][1] - gd.since * (40.0f + 20.0f * (float)k);
                    cv_rect((int32_t)px, oy + (int32_t)py, 2, 2, k & 1 ? PINK3[0] : PINK3[1]);
                }
    for (k = 0; k < 10; k++)            /* fireflies */
        if (fm_sinf(3.0f * t + 1.7f * (float)k) > -0.3f) {
            float fx = 120.0f + 100.0f * fm_sinf(t * (0.3f + 0.07f * (float)k) + 2.1f * (float)k);
            float fy = 80.0f + 40.0f * fm_sinf(t * (0.5f + 0.05f * (float)k) + (float)k);
            cv_rect((int32_t)fx, oy + (int32_t)fy, 2, 2, k & 1 ? RGB(0xd8, 0xff, 0x8f) : RGB(0x9f, 0xff, 0xc0));
        }
}

/* HOME's tracks page (spec 5e; docs/screens/home.html emblems): the four tracks on neon pedestals, each with its
 * scene's emblem turning above it; the pedestal's rim carries the loop's steps; a note makes the emblem hop and a pink
 * ring run over the pedestal; a muted track goes dark. Part of the unity build after the scenes (app_ui.c): sc, sb,
 * PINK3, dn_ball. */

#define HM_FOG RGB(0x0b, 0x06, 0x28)
#define HM_R 0.4f                   /* a pedestal's radius */
#define HM_H 0.62f                  /* its height at LEVEL 127 */
#define HM_HMIN 0.08f               /* at LEVEL 0: still a pedestal */
#define HM_SEGS 352                 /* the grid, the selection, 4 loops of up to 64 steps, the rings */
#define HM_INFO 138                 /* the info row's top in the scene area */
static const uint16_t HM_METAL[3] = {RGB(0x2b, 0x30, 0x70), RGB(0x1f, 0x24, 0x58), RGB(0x15, 0x18, 0x40)};
static const uint16_t HM_DEAD[3] = {RGB(0x1a, 0x16, 0x40), RGB(0x15, 0x12, 0x3a), RGB(0x10, 0x0d, 0x2c)};
static const uint16_t HM_BLUE[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
static const uint16_t HM_GREEN[3] = {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)};
static const uint16_t HM_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const uint16_t HM_ORANGE[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)};
static const uint16_t HM_CYAN[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t HM_DARK[3] = {RGB(0x0b, 0x2a, 0x1f), RGB(0x06, 0x20, 0x1a), RGB(0x03, 0x14, 0x0f)};
enum { HE_FM, HE_KIT, HE_MODAL, HE_SPEECH, HE_VA, HE_WAVE, HE_GARDEN, HE_CHIP, HE_CHOIR, HE_ROPE, HE_DANCE };
static const char *const HM_MODEL[25] = {
    "VA VCF", "PHASE DIST", "6-OP A", "6-OP B", "6-OP C", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "VA", "WAVESHAPE", "2-OP FM", "GRAIN FORM", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT", "KIT"};
static struct {
    float t, since[4], x[4], z[4];
    uint32_t hits[4], hit_ms[4];
    q_hview v;
    int nseg;
} hm;

static int hm_family(int m)
{
    static const uint8_t F[25] = {HE_VA, HE_VA, HE_FM, HE_FM, HE_FM, HE_WAVE, HE_CHOIR, HE_CHIP, HE_VA, HE_VA, HE_FM, HE_GARDEN,
                                  HE_GARDEN, HE_WAVE, HE_CHOIR, HE_SPEECH, HE_DANCE, HE_DANCE, HE_DANCE, HE_ROPE, HE_MODAL,
                                  HE_KIT, HE_KIT, HE_KIT, HE_KIT};
    return F[m];
}

/* the family's colour, for the track's chip */
static uint16_t hm_colour(int fam)
{
    static const uint16_t C[11] = {RGB(0xff, 0x7a, 0x2e), RGB(0x3a, 0x6d, 0xff), RGB(0x22, 0xd5, 0x8e), RGB(0x22, 0xd5, 0x8e),
                                   RGB(0x3a, 0x6d, 0xff), RGB(0xff, 0x7a, 0x2e), RGB(0x9f, 0xf0, 0xff), RGB(0x5f, 0xe0, 0xff),
                                   RGB(0xc9, 0xc3, 0xe4), RGB(0x5f, 0xe0, 0xff), RGB(0x9a, 0x7c, 0xff)};
    return C[fam];
}

/* the emblem's frame: centre o, scale s, turned by a about y; local points to world */
static struct { p3_vec o; float s, c, n, a; } ef;
static p3_vec hm_p(float x, float y, float z)
{
    return (p3_vec){ef.o.x + ef.s * (x * ef.c + z * ef.n), ef.o.y + ef.s * y, ef.o.z + ef.s * (-x * ef.n + z * ef.c)};
}

/* a local box (centre, half sizes), turned with the emblem */
static void hm_box(float x, float y, float z, float hx, float hy, float hz, const uint16_t t[3], const uint16_t *wire)
{
    p3_vec v[8];
    for (int i = 0; i < 8; i++)
        v[i] = hm_p(x + (i & 1 ? hx : -hx), y + (i & 2 ? hy : -hy), z + (i & 4 ? hz : -hz));
    p3d_corners3(v, t, wire);
}

/* an open face of local points, seen from either side */
static void hm_face(const float (*q)[3], int n, const uint16_t t[3], const uint16_t *wire)
{
    p3_vec p[4], r[4];
    for (int i = 0; i < n; i++)
        p[i] = hm_p(q[i][0], q[i][1], q[i][2]), r[n - 1 - i] = p[i];
    if (n == 3) {
        p3d_tri3(p, t, wire, 0.0f);
        p3d_tri3(r, t, wire, 0.0f);
    } else {
        p3d_quad3(p, t, wire, 0.0f);
        p3d_quad3(r, t, wire, 0.0f);
    }
}

/* the emblems, each echoing its family's scene, about 1 unit across */
static void hm_emblem(int fam, const uint16_t *dead, const uint16_t *wire)
{
#define T(x) (dead ? dead : (x))
    switch (fam) {
    case HE_FM:                         /* the planet and its ring */
        dn_ball(ef.o, ef.s, ef.a, T(HM_ORANGE), wire);
        for (int k = 0; k < 12; k++) {
            float a0 = (float)k * (FM_TWO_PI / 12.0f), a1 = a0 + FM_TWO_PI / 12.0f;
            p3d_line3(hm_p(1.6f * fm_cosf(a0), 0.3f * fm_sinf(a0), 1.0f * fm_sinf(a0)), hm_p(1.6f * fm_cosf(a1), 0.3f * fm_sinf(a1), 1.0f * fm_sinf(a1)),
                      2, wire ? *wire : HM_ORANGE[0], 0.0f);
        }
        break;
    case HE_KIT:                        /* a gate of the kit's tunnel, its light at the end */
        for (int k = 0; k < 8; k++) {
            float a0 = ((float)k + 0.5f) * (FM_TWO_PI / 8.0f), a1 = a0 + FM_TWO_PI / 8.0f;
            const float q[4][3] = {{1.1f * fm_cosf(a0), 1.1f * fm_sinf(a0), 0}, {1.1f * fm_cosf(a1), 1.1f * fm_sinf(a1), 0},
                                   {0.7f * fm_cosf(a1), 0.7f * fm_sinf(a1), 0}, {0.7f * fm_cosf(a0), 0.7f * fm_sinf(a0), 0}};
            hm_face(q, 4, T(HM_BLUE), wire);
        }
        if (!dead) {
            const float c[4][3] = {{-0.2f, -0.2f, 0.05f}, {0.2f, -0.2f, 0.05f}, {0.2f, 0.2f, 0.05f}, {-0.2f, 0.2f, 0.05f}};
            hm_face(c, 4, HM_CYAN, 0);
        }
        break;
    case HE_MODAL:                      /* a cut plate, bumps of the mode on it */
        hm_box(0, -0.1f, 0, 0.9f, 0.1f, 0.9f, T(HM_BLUE), wire);
        hm_box(-0.45f, 0.08f, -0.45f, 0.18f, 0.1f, 0.18f, T(HM_GREEN), wire);
        hm_box(0.45f, 0.08f, 0.45f, 0.18f, 0.1f, 0.18f, T(HM_GREEN), wire);
        break;
    case HE_SPEECH:                     /* a head, its dark visor, two pupils */
        hm_box(0, 0, 0, 0.6f, 0.75f, 0.55f, T(HM_GREEN), wire);
        hm_box(0, 0.15f, -0.56f, 0.5f, 0.15f, 0.02f, dead ? dead : HM_DARK, 0);
        if (!dead) {
            hm_box(-0.22f, 0.15f, -0.6f, 0.08f, 0.05f, 0.02f, HM_CYAN, 0);
            hm_box(0.22f, 0.15f, -0.6f, 0.08f, 0.05f, 0.02f, HM_CYAN, 0);
        }
        break;
    case HE_VA: {                       /* a crest: a pyramid, its faces turned out */
        static const float B[4][3] = {{-0.9f, -0.6f, -0.9f}, {0.9f, -0.6f, -0.9f}, {0.9f, -0.6f, 0.9f}, {-0.9f, -0.6f, 0.9f}};
        for (int k = 0; k < 4; k++) {
            const float q[3][3] = {{B[k][0], B[k][1], B[k][2]}, {B[(k + 1) % 4][0], B[(k + 1) % 4][1], B[(k + 1) % 4][2]}, {0, 0.9f, 0}};
            hm_face(q, 3, T(HM_BLUE), wire);
        }
        break;
    }
    case HE_WAVE:                       /* a hoverboard, its glowing underside */
        hm_box(0, 0, 0, 1.1f, 0.09f, 0.38f, T(HM_ORANGE), wire);
        hm_box(0, -0.14f, 0, 0.85f, 0.05f, 0.26f, T(HM_CYAN), 0);
        break;
    case HE_GARDEN:                     /* a crystal flower, facing out, turning in its plane */
        for (int k = 0; k < 6; k++) {
            float a = (float)k * (FM_TWO_PI / 6.0f) + 2.0f * ef.n;
            const float q[3][3] = {{0, 0.1f, 0}, {fm_cosf(a - 0.35f), 0.1f + fm_sinf(a - 0.35f), -0.15f}, {fm_cosf(a + 0.35f), 0.1f + fm_sinf(a + 0.35f), -0.15f}};
            hm_face(q, 3, T(HM_GREEN), wire ? wire : &HM_CYAN[1]);
        }
        {
            const float c[4][3] = {{-0.2f, -0.1f, -0.2f}, {0.2f, -0.1f, -0.2f}, {0.2f, 0.3f, -0.2f}, {-0.2f, 0.3f, -0.2f}};
            hm_face(c, 4, T(HM_ORANGE), 0);
        }
        break;
    case HE_CHIP:                       /* a cube robot, its eyes, its antenna */
        hm_box(0, 0, 0, 0.7f, 0.7f, 0.65f, T(HM_CYAN), wire);
        if (!dead) {
            hm_box(-0.3f, 0.15f, -0.67f, 0.1f, 0.12f, 0.02f, HM_DARK, 0);
            hm_box(0.3f, 0.15f, -0.67f, 0.1f, 0.12f, 0.02f, HM_DARK, 0);
        }
        p3d_line3(hm_p(0, 0.7f, 0), hm_p(0.2f, 1.3f, 0), 1, wire ? *wire : HM_METAL[0], 0.0f);
        break;
    case HE_CHOIR:                      /* an android's head: cyan eyes, an antenna and its ball */
        hm_box(0, 0, 0, 0.7f, 0.65f, 0.6f, T(HM_WHITE), wire);
        if (!dead) {
            hm_box(-0.3f, 0.15f, -0.62f, 0.15f, 0.06f, 0.02f, HM_CYAN, 0);
            hm_box(0.3f, 0.15f, -0.62f, 0.15f, 0.06f, 0.02f, HM_CYAN, 0);
        }
        p3d_line3(hm_p(0, 0.65f, 0), hm_p(0, 1.2f, 0), 1, wire ? *wire : HM_METAL[0], 0.0f);
        hm_box(0, 1.3f, 0, 0.12f, 0.12f, 0.12f, T(HM_ORANGE), 0);
        break;
    case HE_ROPE:                       /* a balancing pole over a laser rope */
        p3d_line3(hm_p(-1.3f, -0.6f, 0), hm_p(1.3f, -0.6f, 0), 2, wire ? *wire : HM_CYAN[1], 0.0f);
        p3d_beam3(hm_p(-1.1f, 0.2f + 0.25f * ef.n, 0), hm_p(1.1f, 0.2f - 0.25f * ef.n, 0), 0.06f * ef.s, T(HM_ORANGE), wire);
        hm_box(0, -0.2f, 0, 0.18f, 0.35f, 0.15f, T(HM_WHITE), wire);
        break;
    default:                            /* the mirror ball */
        dn_ball(ef.o, ef.s, ef.a, T(HM_WHITE), wire ? wire : &HM_CYAN[1]);
        break;
    }
#undef T
}

static void hm_seg(float xa, float ya, float xb, float yb, uint16_t c)
{
    if (hm.nseg == HM_SEGS)
        return;
    int16_t *s = sb.home.seg[hm.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.home.segc[hm.nseg++] = c;
}

static void hm_line(p3_vec a, p3_vec b, uint16_t c)     /* a line projected now, drawn in the band (over the 3D) */
{
    float xa, ya, za, xb, yb, zb;
    if (p3d_project(a, &xa, &ya, &za) && p3d_project(b, &xb, &yb, &zb))
        hm_seg(xa, ya, xb, yb, c);
}

/* a point on a hexagon's outline, u in 0..1 */
static p3_vec hm_hex(float x, float y, float z, float r, float u)
{
    float f = u * 6.0f, k = fm_floorf(f), q = f - k, a = k * (FM_TWO_PI / 6.0f) + FM_PI / 6.0f, b = a + FM_TWO_PI / 6.0f;
    return (p3_vec){x + r * (fm_cosf(a) + (fm_cosf(b) - fm_cosf(a)) * q), y, z + r * (fm_sinf(a) + (fm_sinf(b) - fm_sinf(a)) * q)};
}

/* 1 when a face of points p (a loop) turns out of centre c */
static int hm_out(const p3_vec *p, p3_vec c)
{
    float ux = p[1].x - p[0].x, uy = p[1].y - p[0].y, uz = p[1].z - p[0].z, wx = p[2].x - p[0].x, wy = p[2].y - p[0].y, wz = p[2].z - p[0].z;
    return (uy * wz - uz * wy) * (p[0].x - c.x) + (uz * wx - ux * wz) * (p[0].y - c.y) + (ux * wy - uy * wx) * (p[0].z - c.z) > 0.0f;
}

void home_frame(const q_hview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.0f, 0.2f, 5.0f, 2.1f, 215.0f, 92.0f};
    float t = (float)(ms % 3600000u) * 0.001f;
    sc.fresh = !sc.seen;                /* (HOME has no scene_frame: its own first frame since scene_enter) */
    sc.seen = 1;
    hm.t = t;
    hm.v = *v;
    for (int i = 0; i < 4; i++) {       /* each track's last note (none for those before the page showed) */
        if (sc.fresh || v->hits[i] < hm.hits[i])
            hm.hits[i] = v->hits[i], hm.hit_ms[i] = ms - 100000u;
        else if (v->hits[i] != hm.hits[i])
            hm.hits[i] = v->hits[i], hm.hit_ms[i] = ms;
        hm.since[i] = (float)(ms - hm.hit_ms[i]) * 0.001f;
    }
    sb_owner = SB_HOME;
    hm.nseg = 0;
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.5f, 0.75f, -0.45f}, HM_FOG, 16.0f);
    for (float g = -6.0f; g <= 6.01f; g += 0.75f)       /* the ground's grid, fogged */
        for (int a = 0; a < 2; a++) {
            p3_vec p0 = a ? (p3_vec){g, 0, -2} : (p3_vec){-6, 0, g + 4.0f}, p1 = a ? (p3_vec){g, 0, 16} : (p3_vec){6, 0, g + 4.0f};
            float xa, ya, za, xb, yb, zb;
            if (p3d_project(p0, &xa, &ya, &za) && p3d_project(p1, &xb, &yb, &zb))
                hm_seg(xa, ya, xb, yb, p3d_fogged(RGB(0x2a, 0x1d, 0x80), 0.5f * (za + zb)));
        }
    if (v->sel >= 0 && v->sel < 4) {    /* the selected track: a cyan hexagon on the ground, under the 3D */
        float x = -2.1f + 1.4f * (float)v->sel, z = 0.16f * x * x;
        for (int k = 0; k < 6; k++)
            hm_line(hm_hex(x, 0.01f, z, 0.62f, (float)k / 6.0f), hm_hex(x, 0.01f, z, 0.62f, (float)(k + 1) / 6.0f), RGB(0x5f, 0xe0, 0xff));
    }
    int grid = hm.nseg;
    for (int i = 0; i < 4; i++) {
        float x = -2.1f + 1.4f * (float)i, z = 0.16f * x * x;
        int mute = v->mute[i], sel = i == v->sel, fam = hm_family(v->model[i]);
        float h = HM_HMIN + (HM_H - HM_HMIN) * (float)v->level[i] * (1.0f / 127.0f);   /* the track's LEVEL */
        hm.x[i] = x, hm.z[i] = z;
        /* the pedestal: a hexagonal prism, its faces turned out */
        const uint16_t pw = mute ? RGB(0x24, 0x1c, 0x50) : sel ? RGB(0x2c, 0x9f, 0xd8) : RGB(0x3a, 0x2c, 0x90);
        const uint16_t *pt = mute ? HM_DEAD : HM_METAL;
        for (int k = 0; k < 6; k++) {
            p3_vec b0 = hm_hex(x, 0, z, HM_R * 1.08f, (float)k / 6.0f), b1 = hm_hex(x, 0, z, HM_R * 1.08f, (float)(k + 1) / 6.0f);
            p3_vec t0 = hm_hex(x, h, z, HM_R, (float)k / 6.0f), t1 = hm_hex(x, h, z, HM_R, (float)(k + 1) / 6.0f);
            const p3_vec q[4] = {b0, b1, t1, t0}, r[4] = {t0, t1, b1, b0};
            p3d_quad3(hm_out(q, (p3_vec){x, 0.5f * h, z}) ? q : r, pt, &pw, 0.0f);
        }
        {
            p3_vec hx[6];
            for (int k = 0; k < 6; k++)
                hx[k] = hm_hex(x, h, z, HM_R, (float)k / 6.0f);
            const p3_vec q1[4] = {hx[0], hx[5], hx[4], hx[3]}, q2[4] = {hx[0], hx[3], hx[2], hx[1]};
            uint16_t top[3] = {mute ? HM_DEAD[1] : RGB(0x2b, 0x30, 0x70), mute ? HM_DEAD[1] : RGB(0x2b, 0x30, 0x70), mute ? HM_DEAD[1] : RGB(0x2b, 0x30, 0x70)};
            const p3_vec c = {x, 0.5f * h, z};
            const p3_vec r1[4] = {q1[3], q1[2], q1[1], q1[0]}, r2[4] = {q2[3], q2[2], q2[1], q2[0]};
            int up = hm_out(q1, c);              /* the top, turned out (up) */
            p3d_quad3(up ? q1 : r1, top, 0, 0.0f);
            p3d_quad3(up ? q2 : r2, top, 0, 0.0f);
        }
        /* the loop's steps on the rim: played blue, the current one pink */
        if (!mute && v->len[i] > 0) {
            int cur = v->pos[i] >= 0 ? v->pos[i] % v->len[i] : -1;
            for (int n = 0; n <= v->len[i]; n++) {    /* the current step last: a long loop's steps share pixels */
                int j = n < v->len[i] ? n : cur;
                if (j < 0 || (n < v->len[i] && j == cur))
                    continue;
                uint16_t c = j == cur ? PINK3[1] : j < cur ? RGB(0x3d, 0x7b, 0xff) : RGB(0x2a, 0x20, 0x60);
                float u0 = ((float)j + 0.12f) / (float)v->len[i], u1 = ((float)j + 0.88f) / (float)v->len[i];
                hm_line(hm_hex(x, h + 0.01f, z, HM_R, u0), hm_hex(x, h + 0.01f, z, HM_R, u1), c);
            }
        }
        /* a note: a pink ring runs over the top */
        float s = hm.since[i];
        if (!mute && s < 0.3f) {
            float r = 0.15f + s * 1.6f;
            uint16_t c = p3d_mix(PINK3[1], RGB(0x2b, 0x30, 0x70), s / 0.3f);
            for (int k = 0; k < 12; k++) {
                float a0 = (float)k * (FM_TWO_PI / 12.0f), a1 = a0 + FM_TWO_PI / 12.0f;
                hm_line((p3_vec){x + r * fm_cosf(a0), h + 0.02f, z + r * fm_sinf(a0)}, (p3_vec){x + r * fm_cosf(a1), h + 0.02f, z + r * fm_sinf(a1)}, c);
            }
        }
        /* the emblem: turning, hopping on a note; muted: dark and still, lower */
        float hop = !mute && s < 0.2f ? 0.14f * fm_sinf(FM_PI * s / 0.2f) : 0.0f, a = mute ? 0.5f : 0.8f * t + (float)i;
        ef.o = (p3_vec){x, mute ? h + 0.45f : h + 0.7f + 0.04f * fm_sinf(1.5f * t + (float)i) + hop, z};
        ef.s = 0.42f, ef.c = fm_cosf(a), ef.n = fm_sinf(a), ef.a = a;
        const uint16_t dw = RGB(0x3a, 0x2c, 0x90), pk = PINK3[1];
        hm_emblem(fam, mute ? HM_DEAD : 0, mute ? &dw : !mute && s < 0.05f ? &pk : 0);
    }
    sb.home.grid = grid;
}

void home_band(int32_t oy)
{
    static const uint16_t SKY[4] = {RGB(0x05, 0x03, 0x1a), RGB(0x0b, 0x06, 0x28), RGB(0x14, 0x0a, 0x3a), RGB(0x1c, 0x0d, 0x48)};
    float t = hm.t;
    int hz = 38, i, k;                  /* the horizon: f tan(pitch) above cy */
    cv_rect(0, oy, 240, 22, SKY[0]);
    cv_rect(0, oy + 22, 240, hz - 38, SKY[1]);
    cv_rect(0, oy + hz - 16, 240, 10, SKY[2]);
    cv_rect(0, oy + hz - 6, 240, 6, SKY[3]);
    for (k = 0; k < 24; k++) {          /* stars */
        int32_t x = (int32_t)(p3d_hash((uint32_t)k * 3u + 7000u) * 240.0f), y = 2 + (int32_t)(p3d_hash((uint32_t)k * 5u + 7100u) * (float)(hz - 26));
        cv_rect(x, oy + y, 1, 1, k % 5 ? RGB(0x6c, 0x5f, 0xa0) : RGB(0xff, 0xff, 0xff));
    }
    for (int y = hz - 30; y < hz; y += 3) {     /* the cut sun: slices, the gaps widening downwards */
        float g = fm_maxf(0.0f, (float)(y - (hz - 18)) / 18.0f) * 2.2f, w = fm_sqrtf(fm_maxf(0.0f, 900.0f - (float)((hz - y) * (hz - y))));
        int32_t h = (int32_t)(3.0f - g + 0.5f);
        if (h > 0)
            cv_rect((int32_t)(120.0f - w), oy + y, (int32_t)(2.0f * w), h, p3d_mix(RGB(0xff, 0xb3, 0x47), RGB(0xc2, 0x20, 0x7a), (float)(y - hz + 30) / 30.0f));
    }
    {                                   /* two ridges before it */
        static const int16_t RIDGE[14][2] = {{0, 0}, {0, -9}, {22, -15}, {44, -7}, {70, -18}, {96, -6}, {108, 0},
                                         {140, 0}, {150, -8}, {178, -16}, {200, -6}, {222, -13}, {240, -8}, {240, 0}};
        for (k = 1; k < 6; k++) {
            p3d_fill((float)RIDGE[k][0], (float)(hz + RIDGE[k][1] + oy), (float)RIDGE[k + 1][0], (float)(hz + RIDGE[k + 1][1] + oy), (float)RIDGE[k][0], (float)(hz + oy), RGB(0x0d, 0x07, 0x30));
            p3d_fill((float)RIDGE[k + 1][0], (float)(hz + RIDGE[k + 1][1] + oy), (float)RIDGE[k + 1][0], (float)(hz + oy), (float)RIDGE[k][0], (float)(hz + oy), RGB(0x0d, 0x07, 0x30));
            cv_line(RIDGE[k][0], oy + hz + RIDGE[k][1], RIDGE[k + 1][0], oy + hz + RIDGE[k + 1][1], RGB(0x5a, 0x2f, 0xc0));
        }
        for (k = 8; k < 12; k++) {
            p3d_fill((float)RIDGE[k][0], (float)(hz + RIDGE[k][1] + oy), (float)RIDGE[k + 1][0], (float)(hz + RIDGE[k + 1][1] + oy), (float)RIDGE[k][0], (float)(hz + oy), RGB(0x0d, 0x07, 0x30));
            p3d_fill((float)RIDGE[k + 1][0], (float)(hz + RIDGE[k + 1][1] + oy), (float)RIDGE[k + 1][0], (float)(hz + oy), (float)RIDGE[k][0], (float)(hz + oy), RGB(0x0d, 0x07, 0x30));
            cv_line(RIDGE[k][0], oy + hz + RIDGE[k][1], RIDGE[k + 1][0], oy + hz + RIDGE[k + 1][1], RGB(0x5a, 0x2f, 0xc0));
        }
    }
    cv_rect(0, oy + hz, 240, HM_INFO - hz, RGB(0x07, 0x04, 0x1c));
    for (i = 0; i < sb.home.grid; i++)  /* the grid, under the 3D */
        cv_line(sb.home.seg[i][0], oy + sb.home.seg[i][1], sb.home.seg[i][2], oy + sb.home.seg[i][3], sb.home.segc[i]);
    p3d_draw(oy);
    for (i = sb.home.grid; i < hm.nseg; i++)    /* the steps, the rings, the selection: over it */
        cv_line(sb.home.seg[i][0], oy + sb.home.seg[i][1], sb.home.seg[i][2], oy + sb.home.seg[i][3], sb.home.segc[i]);
    /* the info row: a column a track */
    cv_rect(0, oy + HM_INFO, 240, 166 - HM_INFO, RGB(0x06, 0x04, 0x1a));
    for (i = 0; i < 4; i++) {
        const q_hview *v = &hm.v;
        int32_t x = 4 + 59 * i, y = oy + HM_INFO + 2;
        int mute = v->mute[i];
        char b[16];
        uint16_t chip = mute ? RGB(0x2a, 0x24, 0x50) : i == v->sel ? RGB(0xff, 0x7a, 0x2e) : hm_colour(hm_family(v->model[i]));
        cv_rect(x, y, 18, 13, chip);
        b[0] = (char)('1' + i), b[1] = 0;
        cv_text(x + (18 - text_w(&FONT_S, b)) / 2, y - 1, &FONT_S, b, RGB(0x06, 0x04, 0x1a));
        if (mute) {
            cv_text(x + 22, y - 1, &FONT_S, "MUTE", RGB(0x5e, 0x57, 0x8a));
        } else {
            b[0] = 'P', b[1] = (char)('0' + v->pat[i]), b[2] = 0;
            int32_t e = cv_text(x + 22, y - 1, &FONT_S, b, RGB(0xf4, 0xf1, 0xff));
            if (v->next[i] && fm_floorf(t * 4.0f) - 2.0f * fm_floorf(t * 2.0f) > 0.5f) {   /* the queued one, blinking */
                b[0] = '>', b[1] = (char)('0' + v->next[i]), b[2] = 0;
                cv_text(e + 1, y - 1, &FONT_S, b, RGB(0xff, 0x7a, 0x2e));
            }
        }
        const char *name = HM_MODEL[v->model[i]];
        int n = 0;
        while (name[n] && n < 15) b[n] = name[n], n++;
        b[n] = 0;
        while (n > 1 && text_w(&FONT_XS, b) > 55)    /* cut to the column */
            b[--n] = 0;
        cv_text(x, y + 12, &FONT_XS, b, mute ? RGB(0x5e, 0x57, 0x8a) : RGB(0x8b, 0x84, 0xa8));
    }
}

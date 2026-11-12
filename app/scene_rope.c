/* STRING's scene (spec 5d2, 3; docs/screens/string.html walker): an android acrobat on a laser rope under a wire big
 * top; each note it hops and plucks the rope under it, which rings by the string's own modes. Part of the unity build
 * after scene.c (app_ui.c): its sc, sb, stars, PINK3. */

#define RP_FOG RGB(0x0a, 0x06, 0x26)
#define RP_Y 1.55f                  /* the rope's height at its ends */
#define RP_SEGS 30
static const uint16_t RP_RING[2][3] = {{RGB(0x9a, 0x7c, 0xff), RGB(0x6a, 0x4e, 0xe0), RGB(0x3a, 0x2a, 0x8a)},
                                       {RGB(0x9f, 0xf0, 0xff), RGB(0x3f, 0xb8, 0xe0), RGB(0x1d, 0x6f, 0x98)}};
static const uint16_t RP_POLE[3] = {RGB(0x2b, 0x30, 0x70), RGB(0x1f, 0x24, 0x58), RGB(0x15, 0x18, 0x40)};
static const uint16_t RP_BLUE[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
static const uint16_t RP_STRIPE[2][3] = {{RGB(0x9f, 0xf0, 0xff), RGB(0x3f, 0xb8, 0xe0), RGB(0x1d, 0x6f, 0x98)},
                                         {RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xe3, 0xf8), RGB(0xb9, 0xb0, 0xd8)}};
static const uint16_t RP_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const uint16_t RP_CYAN[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t RP_ORANGE[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)};
static struct {
    float t, since, since_prev, pluck, pluck_prev, hop, shown, k[4], spot[2], disc[32];   /* shown: the acrobat's height */
    int nseg, spot_seen, disc_seen;
    uint32_t hits, hop_ms, pluck_ms, prev_ms;
} rp;

/* the rope's ringing: six modes, HARM their stiffness, TIMB the brightness, MORPH the damping. Struck, not plucked: it
   starts from where it is (sin; the mock-up's cos jumped). Each mode's amplitude at a time, for the last strike and
   the one before, computed once a frame */
static float rp_mode[2][6];

static void rp_modes(float s, float p, float *m)
{
    for (int k = 1; k <= 6; k++) {
        float fk = (float)k, tau = (0.12f + 1.6f * (1.0f - rp.k[2])) / fm_sqrtf(fk);
        float w = 22.0f * fk * (1.0f + 0.12f * rp.k[0] * fk * fk);
        m[k - 1] = s < 8.0f * tau ? fm_powf(fk, -2.2f + 1.9f * rp.k[1]) * fm_sinf(fk * FM_PI * p) * fm_sinf(w * s) * fm_expf(-s / tau) : 0.0f;
    }
}

/* the rope at q: its sag, the last strike ringing, the one before still dying away */
static float rp_rope_y(float q)
{
    float y = 0.0f;
    for (int k = 0; k < 6; k++)
        y += (rp_mode[0][k] + rp_mode[1][k]) * fm_sinf((float)(k + 1) * FM_PI * q);
    return RP_Y - 0.05f * fm_sinf(q * FM_PI) + 0.2f * y;
}

static void rp_seg(p3_vec a, p3_vec b, uint16_t c, float bias)     /* a backdrop line, drawn in the band */
{
    float xa, ya, za, xb, yb, zb;
    if (rp.nseg == 64 || !p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    int16_t *s = sb.rope.seg[rp.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.rope.segc[rp.nseg++] = p3d_fogged(c, 0.5f * (za + zb) - bias);
}

/* k: HARM (the rope's stiffness), TIMB (its brightness), MORPH (its damping), DECAY (the pink) */
void rope_frame(const q_sview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.0f, 0.14f, 3.5f, 1.7f, 200.0f, 90.0f};
    float t = (float)(ms % 3600000u) * 0.001f, z;
    for (int i = 0; i < 4; i++)
        rp.k[i] = (float)v->k[i] / 127.0f;
    rp.t = t;
    float u = 0.5f + 0.36f * fm_sinf(0.35f * t), px = -1.6f + 3.2f * u;
    if (sc.fresh) {                     /* just shown: the rope at rest, no hop */
        rp.hits = sc.hits[0];
        rp.hop_ms = rp.pluck_ms = rp.prev_ms = ms - 100000u;
        rp.pluck = rp.pluck_prev = u;
    } else if (sc.hits[0] != rp.hits) { /* a note: struck under the acrobat, the last strike left ringing; a hop if none */
        rp.hits = sc.hits[0];           /* is under way */
        rp.pluck_prev = rp.pluck, rp.prev_ms = rp.pluck_ms;
        rp.pluck = u, rp.pluck_ms = sc.hit_ms[0];
        if (ms - rp.hop_ms >= 200u)
            rp.hop_ms = sc.hit_ms[0];
    }
    rp.since = (float)(ms - rp.pluck_ms) * 0.001f;
    rp.since_prev = (float)(ms - rp.prev_ms) * 0.001f;
    rp_modes(rp.since, rp.pluck, rp_mode[0]);
    rp_modes(rp.since_prev, rp.pluck_prev, rp_mode[1]);
    float hs = (float)(ms - rp.hop_ms) * 0.001f;
    rp.hop = hs < 0.2f ? 0.12f * fm_sinf(FM_PI * hs / 0.2f) : 0.0f;
    sb_owner = SB_ROPE;
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.5f, 0.75f, -0.45f}, RP_FOG, 14.0f);
    rp.nseg = 0;                        /* the backdrop: the big top's back half, the ground's grid */
    for (int k = 0; k <= 8; k++) {
        float a0 = (float)k * (FM_TWO_PI / 16.0f), a1 = a0 + FM_TWO_PI / 16.0f;
        p3_vec b0 = {3.4f * fm_cosf(a0), 0, 0.9f + 2.0f * fm_sinf(a0)}, b1 = {3.4f * fm_cosf(a1), 0, 0.9f + 2.0f * fm_sinf(a1)}, ap = {0, 3.8f, 0.9f};
        rp_seg(b0, ap, RGB(0x4a, 0x36, 0xb0), 0.0f);
        if (k < 8)
            for (int r = 0; r < 2; r++) {
                float q = r ? 0.7f : 0.35f;
                rp_seg((p3_vec){b0.x * (1 - q), ap.y * q, b0.z + (ap.z - b0.z) * q}, (p3_vec){b1.x * (1 - q), ap.y * q, b1.z + (ap.z - b1.z) * q},
                       RGB(0x35, 0x2a, 0x8a), 0.0f);
            }
    }
    for (int i = 0; i < 17; i++)
        rp_seg((p3_vec){-3.6f + 0.45f * (float)i, 0, -1.5f}, (p3_vec){-3.6f + 0.45f * (float)i, 0, 3.0f}, RGB(0x22, 0x18, 0x6a), -50.0f);
    for (int i = 0; i < 11; i++)
        rp_seg((p3_vec){-3.6f, 0, -1.5f + 0.45f * (float)i}, (p3_vec){3.6f, 0, -1.5f + 0.45f * (float)i}, RGB(0x22, 0x18, 0x6a), -50.0f);
    rp.disc_seen = 1;                   /* the ring's floor, in the band */
    for (int k = 0; k < 16; k++) {
        float a = (float)k * (FM_TWO_PI / 16.0f);
        rp.disc_seen &= p3d_project((p3_vec){2.3f * fm_cosf(a), 0.001f, 0.4f + 1.265f * fm_sinf(a)}, &rp.disc[2 * k], &rp.disc[2 * k + 1], &z);
    }
    for (int k = 0; k < 16; k++) {      /* the ring's neon wall, violet and cyan */
        float a0 = (float)k * (FM_TWO_PI / 16.0f), a1 = a0 + FM_TWO_PI / 16.0f;
        p3_vec p0 = {2.3f * fm_cosf(a0), 0, 0.4f + 1.265f * fm_sinf(a0)}, p1 = {2.3f * fm_cosf(a1), 0, 0.4f + 1.265f * fm_sinf(a1)};
        const p3_vec q[4] = {p0, p1, {p1.x, 0.18f, p1.z}, {p0.x, 0.18f, p0.z}}, r[4] = {q[3], q[2], q[1], q[0]};
        p3d_quad3(q, RP_RING[k & 1], 0, 0.0f);
        p3d_quad3(r, RP_RING[k & 1], 0, 0.0f);
    }
    const uint16_t cyan = RGB(0x5f, 0xe0, 0xff);
    for (int s = -1; s <= 1; s += 2) {  /* the two masts and their platforms */
        float x = 1.6f * (float)s;
        p3d_beam3((p3_vec){x, 0, 0}, (p3_vec){x, 1.6f, 0}, 0.05f, RP_POLE, 0);
        p3d_line3((p3_vec){x, 0, -0.06f}, (p3_vec){x, 1.55f, -0.06f}, 1, RGB(0x7a, 0x5c, 0xff), 0.0f);
        p3d_box3((p3_vec){1.65f * (float)s, 1.55f, 0}, (p3_vec){0.22f, 0.03f, 0.2f}, RP_POLE, &cyan);
    }
    float pink = 0.1f + 0.4f * rp.k[3];
    float ry0 = rp_rope_y(0.0f);
    for (int i = 0; i < RP_SEGS; i++) { /* the laser rope, pink where it was just struck */
        float q0 = (float)i / RP_SEGS, q1 = (float)(i + 1) / RP_SEGS, mid = 0.5f * (q0 + q1), ry1 = rp_rope_y(q1);
        uint16_t c = rp.since < pink && fm_fabsf(mid - rp.pluck) < 0.06f ? PINK3[1] : cyan;
        p3d_line3((p3_vec){-1.6f + 3.2f * q0, ry0, 0}, (p3_vec){-1.6f + 3.2f * q1, ry1, 0}, 2, c, 0.0f);
        ry0 = ry1;
    }
    /* the acrobat: stepping along, balancing its pole, hopping on a note */
    float ry = rp_rope_y(u) + rp.hop, hip = ry + 0.3f, neck = ry + 0.56f, poleY = ry + 0.47f, tilt = 0.12f * fm_sinf(1.7f * t);
    rp.shown = ry;
    for (int s = -1; s <= 1; s += 2)
        p3d_beam3((p3_vec){px + 0.05f * (float)s, ry, 0}, (p3_vec){px, hip, 0}, 0.028f, RP_BLUE, 0);
    for (int k = 0; k < 4; k++)
        p3d_box3((p3_vec){px, hip + 0.03f + 0.065f * (float)k, 0}, (p3_vec){0.06f, 0.032f, 0.05f}, RP_STRIPE[k & 1], 0);
    p3_vec hl = {px - 0.2f, poleY + 0.2f * tilt, -0.12f}, hr = {px + 0.2f, poleY - 0.2f * tilt, -0.12f};
    p3d_beam3((p3_vec){px - 0.05f, neck - 0.04f, 0}, hl, 0.02f, RP_STRIPE[0], 0);
    p3d_beam3((p3_vec){px + 0.05f, neck - 0.04f, 0}, hr, 0.02f, RP_STRIPE[0], 0);
    p3d_box3((p3_vec){px, neck + 0.08f, 0}, (p3_vec){0.055f, 0.055f, 0.055f}, RP_WHITE, 0);
    p3d_box3((p3_vec){px + 0.04f, neck + 0.09f, 0}, (p3_vec){0.025f, 0.02f, 0.05f}, RP_CYAN, 0);
    p3d_beam3((p3_vec){px - 0.75f, poleY + tilt, -0.12f}, (p3_vec){px + 0.75f, poleY - tilt, -0.12f}, 0.012f, RP_ORANGE, 0);
    rp.spot_seen = p3d_project((p3_vec){px, RP_Y + 0.4f, 1.4f}, &rp.spot[0], &rp.spot[1], &z);
}

void rope_band(int32_t oy)
{
    float o = (float)oy;
    int i, k;
    cv_rect(0, oy, 240, 90, RGB(0x05, 0x03, 0x1a));
    cv_rect(0, oy + 90, 240, 76, RP_FOG);
    stars(oy, rp.t, 30, 3, 56, 1.0f);
    for (i = 0; i < rp.nseg; i++)
        cv_line(sb.rope.seg[i][0], oy + sb.rope.seg[i][1], sb.rope.seg[i][2], oy + sb.rope.seg[i][3], sb.rope.segc[i]);
    if (rp.disc_seen)                   /* the ring's floor */
        for (k = 1; k < 15; k++)
            p3d_fill(rp.disc[0], rp.disc[1] + o, rp.disc[2 * k], rp.disc[2 * k + 1] + o, rp.disc[2 * k + 2], rp.disc[2 * k + 3] + o,
                     RGB(0x0e, 0x0a, 0x26));
    p3d_draw(oy);
    if (rp.spot_seen)                   /* the follow-spot on the acrobat */
        for (k = 0; k < 8; k++) {
            float a0 = (float)k * (FM_TWO_PI / 8.0f) + FM_PI / 8.0f, a1 = a0 + FM_TWO_PI / 8.0f;
            cv_line((int32_t)(rp.spot[0] + 26.0f * fm_cosf(a0)), oy + (int32_t)(rp.spot[1] + 32.0f * fm_sinf(a0)),
                    (int32_t)(rp.spot[0] + 26.0f * fm_cosf(a1)), oy + (int32_t)(rp.spot[1] + 32.0f * fm_sinf(a1)), RGB(0x3f, 0xa8, 0xff));
        }
    for (k = 0; k <= 10; k++) {         /* the audience, in front */
        float x = (float)(23 * k - 4 + 6 * (k % 2)), y = (float)(156 - 3 * (k % 3));
        p3d_fill(x - 10, 166 + o, x - 8, y + o, x + 8, y + o, RGB(0x0a, 0x04, 0x10));
        p3d_fill(x - 10, 166 + o, x + 8, y + o, x + 10, 166 + o, RGB(0x0a, 0x04, 0x10));
        cv_rect((int32_t)x - 5, oy + (int32_t)y - 11, 10, 9, RGB(0x0a, 0x04, 0x10));
        cv_rect((int32_t)x - 3, oy + (int32_t)y - 8, 6, 1, RGB(0x3f, 0xa8, 0xff));
    }
}

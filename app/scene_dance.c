/* The noise engines' scene (spec 5d2, 4; docs/screens/noise.html dance): a neon dancefloor whose tiles the grains
 * light, a DJ before an LED wall, three dancers, a mirror ball. NOISE (snow), PARTICLE (sparks), SWARM (a swarm). Part
 * of the unity build after scene.c (app_ui.c): its sc, sb, PINK3. */

#define DN_FOG RGB(0x0a, 0x06, 0x26)
#define DN_NX 8
#define DN_NZ 6
#define DN_TILES (DN_NX * DN_NZ)
static const uint16_t DN_K[4][3] = {{RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)},
                                    {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)},
                                    {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)},
                                    {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)}};
static const uint16_t DN_CYAN[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t DN_METAL[3] = {RGB(0x2b, 0x30, 0x70), RGB(0x1f, 0x24, 0x58), RGB(0x15, 0x18, 0x40)};
static const uint16_t DN_DIM[3] = {RGB(0x3b, 0x31, 0x70), RGB(0x2e, 0x26, 0x58), RGB(0x1f, 0x19, 0x46)};
static const uint16_t DN_VIOLET[3] = {RGB(0x9a, 0x7c, 0xff), RGB(0x6a, 0x4e, 0xe0), RGB(0x3a, 0x2a, 0x8a)};
static struct {
    float t, since;
    int hot, nseg, nwall, ncell;
} dn;

/* a grain's tile from its x, z in -1..1: -1 off the floor (a swarm can stray beyond it) */
static int dance_tile(float x, float z)
{
    if (x < -1.0f || x > 1.0f || z < -1.0f || z > 1.0f)
        return -1;
    int i = (int)((x + 1.0f) * 0.5f * DN_NX), j = (int)((z + 1.0f) * 0.5f * DN_NZ);
    return (i < DN_NX ? i : DN_NX - 1) + DN_NX * (j < DN_NZ ? j : DN_NZ - 1);
}

static float dn_rnd(int i, int k) { return p3d_hash((uint32_t)i * 2654435761u ^ (uint32_t)k * 40503u ^ 0x5bd1e995u); }

/* a grain lights its tile: the strongest one keeps it, with its height; trail grains dimmer (b < 0.6) */
static void dn_grain(float x, float y, float z, float b)
{
    int k = dance_tile(x, z);
    if (k >= 0 && b > sb.dance.b[k])
        sb.dance.b[k] = b, sb.dance.y[k] = y;
}

/* the swarm's grains at time s: round a wandering centre, HARM its radius, TIMB how many */
static void dn_swarm(const float k[4], float s, float b)
{
    int n = (int)(8.0f + 24.0f * k[1] + 0.5f);
    float cx = 0.45f * fm_sinf(0.7f * s), cy = 0.3f * fm_sinf(1.1f * s), cz = 0.45f * fm_cosf(0.5f * s), r = 0.12f + 0.7f * k[0];
    for (int i = 0; i < n; i++) {
        float a = s * (1.0f + dn_rnd(i, 1)) + (float)i, be = fm_sinf(s * (0.8f + dn_rnd(i, 2)) + 2.0f * (float)i), cb = fm_cosf(be);
        dn_grain(cx + r * fm_cosf(a) * cb, cy + 0.6f * r * fm_sinf(be), cz + r * fm_sinf(a) * cb, b);
    }
}

static void dn_seg(p3_vec a, p3_vec b, uint16_t c, float bias)     /* a backdrop line, drawn in the band */
{
    float xa, ya, za, xb, yb, zb;
    if (dn.nseg == 48 || !p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    int16_t *s = sb.dance.seg[dn.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.dance.segc[dn.nseg++] = p3d_fogged(c, 0.5f * (za + zb) - bias);
}

/* a ring of four segments (a woofer, a deck): centre c, radii rx, ry in its plane (xy if flat, else xz), turned by a */
static void dn_ring(p3_vec c, float rx, float ry, int flat, float a, uint16_t col, float bias)
{
    for (int q = 0; q < 4; q++) {
        float a0 = a + (float)q * (0.5f * FM_PI), a1 = a0 + 0.5f * FM_PI;
        p3_vec p0 = c, p1 = c;
        p0.x += rx * fm_cosf(a0), p1.x += rx * fm_cosf(a1);
        if (flat)
            p0.z += ry * fm_sinf(a0), p1.z += ry * fm_sinf(a1);
        else
            p0.y += ry * fm_sinf(a0), p1.y += ry * fm_sinf(a1);
        p3d_line3(p0, p1, 1, col, bias);
    }
}

/* the mirror ball: an icosahedron, its faces turned out (also HOME's FM planet and mirror ball) */
static void dn_ball(p3_vec c, float r, float a, const uint16_t tones[3], const uint16_t *wire)
{
    static const int8_t V[12][3] = {{-1, 2, 0}, {1, 2, 0}, {-1, -2, 0}, {1, -2, 0}, {0, -1, 2}, {0, 1, 2},
                                    {0, -1, -2}, {0, 1, -2}, {2, 0, -1}, {2, 0, 1}, {-2, 0, -1}, {-2, 0, 1}};   /* 2: the golden ratio */
    static const uint8_t F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2},
                                     {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5},
                                     {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    p3_vec v[12];
    float ca = fm_cosf(a), sa = fm_sinf(a), n = r / 1.9021130f;     /* |(1, phi, 0)| */
    for (int i = 0; i < 12; i++) {
        float x = (float)V[i][0] * (V[i][0] == 2 || V[i][0] == -2 ? 0.809017f : 1.0f);
        float y = (float)V[i][1] * (V[i][1] == 2 || V[i][1] == -2 ? 0.809017f : 1.0f);
        float z = (float)V[i][2] * (V[i][2] == 2 || V[i][2] == -2 ? 0.809017f : 1.0f);
        v[i] = (p3_vec){c.x + n * (x * ca + z * sa), c.y + n * y, c.z + n * (-x * sa + z * ca)};
    }
    for (int f = 0; f < 20; f++) {
        p3_vec t[3] = {v[F[f][0]], v[F[f][1]], v[F[f][2]]};
        float ux = t[1].x - t[0].x, uy = t[1].y - t[0].y, uz = t[1].z - t[0].z, wx = t[2].x - t[0].x, wy = t[2].y - t[0].y, wz = t[2].z - t[0].z;
        float d = (uy * wz - uz * wy) * (t[0].x - c.x) + (uz * wx - ux * wz) * (t[0].y - c.y) + (ux * wy - uy * wx) * (t[0].z - c.z);
        if (d < 0.0f) {
            p3_vec s = t[1];
            t[1] = t[2], t[2] = s;
        }
        p3d_tri3(t, tones, wire, 0.0f);
    }
}

/* k: HARM, TIMB, MORPH (the grains: see dn_swarm and below), DECAY (how long the dancers' arms stay up) */
void dance_frame(const q_sview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.0f, 0.36f, 3.1f, 2.2f, 215.0f, 102.0f};
    float t = (float)(ms % 3600000u) * 0.001f, k[4], z;
    for (int i = 0; i < 4; i++)
        k[i] = (float)v->k[i] / 127.0f;
    dn.t = t;
    dn.since = sc.fresh ? 100.0f : (float)(ms - sc.hit_ms[0]) * 0.001f;
    float heat = dn.since < 3.0f ? fm_expf(-dn.since / (0.05f + 0.4f * k[3])) : 0.0f;
    dn.hot = heat > 0.4f;
    sb_owner = SB_DANCE;
    for (int i = 0; i < DN_TILES; i++)
        sb.dance.b[i] = 0.0f;
    if (v->model == 17) {               /* NOISE: a snow drawn anew 3 to 28 times a second, in a band (HARM, MORPH) */
        int n = (int)(10.0f + 50.0f * k[1] + 0.5f), f = (int)fm_floorf(t * (3.0f + 25.0f * k[1]));
        float c = -0.7f + 1.4f * k[0], w = 0.15f + 0.85f * (1.0f - k[2]);
        for (int i = 0; i < n; i++)
            dn_grain(dn_rnd(i, f) * 2.0f - 1.0f, fm_clampf(c + (dn_rnd(i, f + 33) * 2.0f - 1.0f) * w, -1.0f, 1.0f),
                     dn_rnd(i, f + 77) * 2.0f - 1.0f, 1.0f);
    } else if (v->model == 18) {        /* PARTICLE: sparks, each lit then fading (MORPH: how fast) */
        int n = (int)(10.0f + 50.0f * k[1] + 0.5f);
        for (int i = 0; i < n; i++) {
            float per = 0.4f + 0.9f * dn_rnd(i, 5), ph = t / per + dn_rnd(i, 6), cyc = fm_floorf(ph), a = ph - cyc;
            int cy = (int)cyc;
            dn_grain(dn_rnd(i, cy) * 2.0f - 1.0f, (dn_rnd(i, cy + 9) * 2.0f - 1.0f) * (0.15f + 0.85f * k[0]), dn_rnd(i, cy + 17) * 2.0f - 1.0f,
                     fm_expf(-a * (3.0f + 12.0f * k[2])));
        }
    } else {                            /* SWARM: and its trail, where it was (MORPH: how long ago) */
        dn_swarm(k, t - 0.05f - 0.3f * k[2], 0.5f);
        dn_swarm(k, t, 1.0f);
    }
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.5f, 0.75f, -0.45f}, DN_FOG, 12.0f);
    /* the backdrop: the LED wall's cells, the unlit tiles, the neons, the sweeping beams */
    dn.nwall = 0;
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 14; i++) {
            float x0 = -2.6f + 5.2f * ((float)i + 0.08f) / 14.0f, y0 = 0.2f + 2.3f * ((float)j + 0.08f) / 6.0f, ax, ay, bx, by;
            if (!p3d_project((p3_vec){x0, y0, 2.4f}, &ax, &ay, &z) || !p3d_project((p3_vec){x0 + 0.312f, y0 + 0.322f, 2.4f}, &bx, &by, &z))
                continue;
            float wave = 0.5f + 0.5f * fm_sinf(0.6f * (float)i - 2.0f * t + fm_sinf(0.8f * (float)j + t));
            int16_t *c = sb.dance.cell[dn.nwall];
            c[0] = (int16_t)ax, c[1] = (int16_t)by, c[2] = (int16_t)(bx - ax + 0.5f), c[3] = (int16_t)(ay - by + 0.5f);
            sb.dance.cellc[dn.nwall++] = p3d_fogged(p3d_mix(j & 1 ? RGB(0x3f, 0xa8, 0xff) : RGB(0x7a, 0x5c, 0xff), RGB(0x0d, 0x0a, 0x2a),
                                                            0.4f + 0.5f * (1.0f - wave)), z);
        }
    dn.ncell = 0;
    dn.nseg = 0;
    for (int i = 0; i < DN_NX; i++)
        for (int j = 0; j < DN_NZ; j++) {
            float x = -1.9f + 0.475f * (float)i, zz = -1.4f + 0.4667f * (float)j, dx = 0.437f, dz = 0.4293f;
            if (sb.dance.b[i + DN_NX * j] > 0.15f)
                continue;
            float *q = sb.dance.tile[dn.ncell];
            int ok = p3d_project((p3_vec){x, 0, zz}, &q[0], &q[1], &z) && p3d_project((p3_vec){x + dx, 0, zz}, &q[2], &q[3], &z) &&
                     p3d_project((p3_vec){x + dx, 0, zz + dz}, &q[4], &q[5], &z) && p3d_project((p3_vec){x, 0, zz + dz}, &q[6], &q[7], &z);
            if (ok)
                sb.dance.tilec[dn.ncell++] = p3d_fogged(DN_METAL[0], z);
        }
    dn_seg((p3_vec){-2.68f, 0.12f, 2.39f}, (p3_vec){2.68f, 0.12f, 2.39f}, RGB(0x5f, 0xe0, 0xff), 0.0f);
    dn_seg((p3_vec){2.68f, 0.12f, 2.39f}, (p3_vec){2.68f, 2.58f, 2.39f}, RGB(0x5f, 0xe0, 0xff), 0.0f);
    dn_seg((p3_vec){2.68f, 2.58f, 2.39f}, (p3_vec){-2.68f, 2.58f, 2.39f}, RGB(0x5f, 0xe0, 0xff), 0.0f);
    dn_seg((p3_vec){-2.68f, 2.58f, 2.39f}, (p3_vec){-2.68f, 0.12f, 2.39f}, RGB(0x5f, 0xe0, 0xff), 0.0f);
    for (int s = 0; s < 6; s++) {
        float x = s < 3 ? -2.85f : 2.85f, zz = 0.2f + 0.7f * (float)(s % 3);
        dn_seg((p3_vec){x, 0, zz}, (p3_vec){x, 2.6f, zz}, s & 1 ? RGB(0x7a, 0x5c, 0xff) : RGB(0x3f, 0xa8, 0xff), 0.0f);
    }
    /* (the truss and its five projectors hang above the frame, at y = -16: not drawn) */
    for (int b = 0; b < 2; b++)
        dn_seg((p3_vec){b ? 2.4f : -2.4f, 2.6f, 1.4f}, (p3_vec){1.4f * fm_sinf(0.9f * t + 2.0f * (float)b), 0, 0.8f * fm_cosf(0.7f * t + (float)b)},
               b ? RGB(0x7a, 0x5c, 0xff) : RGB(0x3f, 0xa8, 0xff), 0.0f);
    /* the lit tiles: their top, coloured by their grain's height, pink on a note */
    int rays = 0;
    const uint16_t bwire = RGB(0x3f, 0xa8, 0xff);
    dn_ball((p3_vec){-1.0f, 2.1f, 0.6f}, 0.32f, 0.8f * t, DN_K[2], &bwire);
    p3d_line3((p3_vec){-1.0f, 2.42f, 0.6f}, (p3_vec){-1.0f, 4.0f, 0.6f}, 1, DN_K[2][2], 0.0f);
    for (int i = 0; i < DN_NX; i++)
        for (int j = 0; j < DN_NZ; j++) {
            int idx = i + DN_NX * j;
            float b = sb.dance.b[idx];
            if (b <= 0.15f)
                continue;
            static const uint16_t *const BAND[3] = {DN_VIOLET, DN_CYAN, DN_K[1]};
            int band = (int)((sb.dance.y[idx] + 1.0f) * 0.5f * 3.0f);
            const uint16_t *tn = BAND[band < 0 ? 0 : band > 2 ? 2 : band];
            uint16_t trail[3] = {tn[1], tn[1], tn[1]};
            if (dn.since < 0.08f)
                tn = PINK3;
            else if (b < 0.6f)
                tn = trail;
            float x = -1.9f + 0.475f * (float)i, zz = -1.4f + 0.4667f * (float)j;
            const p3_vec q[4] = {{x, 0.05f, zz}, {x, 0.05f, zz + 0.4293f}, {x + 0.437f, 0.05f, zz + 0.4293f}, {x + 0.437f, 0.05f, zz}};
            const uint16_t wire = dn.since < 0.08f ? PINK3[0] : tn[0];
            p3d_quad3(q, tn, &wire, 0.0f);
            if (dn.since < 0.2f && rays < 6) {  /* the ball's rays to the first lit tiles */
                p3d_line3((p3_vec){-1.0f, 2.1f, 0.6f}, (p3_vec){x + 0.22f, 0.05f, zz + 0.21f}, 1, PINK3[1], 0.5f);
                rays++;
            }
        }
    const uint16_t lav = RGB(0x7a, 0x5c, 0xff), cyl = RGB(0x5f, 0xe0, 0xff);
    for (int s = -1; s <= 1; s += 2) {  /* the speakers and their woofers */
        p3d_box3((p3_vec){2.35f * (float)s, 0.55f, 0.6f}, (p3_vec){0.28f, 0.55f, 0.28f}, DN_METAL, &lav);
        dn_ring((p3_vec){2.35f * (float)s, 0.35f, 0.31f}, 0.16f * (dn.hot ? 1.15f : 1.0f), 0.16f, 0, 0.0f, cyl, 0.3f);
        dn_ring((p3_vec){2.35f * (float)s, 0.8f, 0.31f}, 0.16f * (dn.hot ? 1.15f : 1.0f), 0.16f, 0, 0.0f, cyl, 0.3f);
    }
    /* the DJ at the console */
    p3d_box3((p3_vec){0, 0.35f, 1.75f}, (p3_vec){0.6f, 0.35f, 0.18f}, DN_METAL, &cyl);
    dn_ring((p3_vec){-0.3f, 0.71f, 1.75f}, 0.12f, 0.1f, 1, 4.0f * t, DN_VIOLET[0], 0.2f);
    dn_ring((p3_vec){0.3f, 0.71f, 1.75f}, 0.12f, 0.1f, 1, 4.0f * t, DN_VIOLET[0], 0.2f);
    float nod = fm_fabsf(fm_sinf(6.3f * t)) * 0.04f;
    p3d_box3((p3_vec){0, 0.95f, 1.95f}, (p3_vec){0.14f, 0.2f, 0.08f}, DN_VIOLET, 0);
    p3d_box3((p3_vec){0, 1.25f - nod, 1.95f}, (p3_vec){0.09f, 0.09f, 0.08f}, DN_K[2], 0);
    p3d_box3((p3_vec){0, 1.26f - nod, 1.86f}, (p3_vec){0.07f, 0.02f, 0.01f}, DN_CYAN, 0);
    p3d_box3((p3_vec){-0.11f, 1.25f - nod, 1.95f}, (p3_vec){0.02f, 0.06f, 0.06f}, DN_DIM, 0);
    p3d_box3((p3_vec){0.11f, 1.25f - nod, 1.95f}, (p3_vec){0.02f, 0.06f, 0.06f}, DN_DIM, 0);
    /* three dancers, bouncing; arms up on a note */
    static const float DX[3] = {-0.7f, 0.65f, 0.05f}, DZ[3] = {0.05f, -0.3f, -0.75f};
    for (int i = 0; i < 3; i++) {
        const uint16_t *tn = DN_K[i == 2 ? 3 : i];
        float x = DX[i], zz = DZ[i], bob = fm_fabsf(fm_sinf(6.3f * t + (float)i)) * (i ? 0.05f : 0.1f);
        float sway = fm_sinf(t * (i == 1 ? 3.1f : 1.6f) + (float)i) * (i == 1 ? 0.14f : 0.06f), up = dn.hot ? 0.2f : 0.05f * fm_sinf(6.3f * t + (float)i);
        for (int s = -1; s <= 1; s += 2)
            p3d_beam3((p3_vec){x + 0.07f * (float)s, 0, zz}, (p3_vec){x + 0.5f * sway, 0.3f + bob, zz}, 0.03f, DN_DIM, 0);
        p3d_box3((p3_vec){x + sway, 0.45f + bob, zz}, (p3_vec){0.1f, 0.15f, 0.07f}, tn, 0);
        p3d_line3((p3_vec){x + sway - 0.08f, 0.55f + bob, zz}, (p3_vec){x + sway - 0.18f, 0.75f + bob + up, zz}, 2, tn[1], 0.0f);
        p3d_line3((p3_vec){x + sway + 0.08f, 0.55f + bob, zz}, (p3_vec){x + sway + 0.18f, 0.62f + bob + 0.5f * up, zz}, 2, tn[1], 0.0f);
        p3d_box3((p3_vec){x + sway, 0.69f + bob, zz}, (p3_vec){0.07f, 0.07f, 0.07f}, DN_K[2], 0);
        p3d_box3((p3_vec){x + sway + 0.05f, 0.7f + bob, zz - 0.05f}, (p3_vec){0.02f, 0.015f, 0.025f}, DN_CYAN, 0);
    }
}

void dance_band(int32_t oy)
{
    float t = dn.t, o = (float)oy;
    int i, k;
    cv_rect(0, oy, 240, 120, RGB(0x05, 0x03, 0x1a));
    cv_rect(0, oy + 120, 240, 46, DN_FOG);
    for (i = 0; i < dn.nwall; i++) {    /* the LED wall */
        const int16_t *c = sb.dance.cell[i];
        cv_rect(c[0], oy + c[1], c[2], c[3], sb.dance.cellc[i]);
    }
    for (i = 0; i < dn.ncell; i++) {    /* the unlit tiles: metal, a blue wire */
        const float *q = sb.dance.tile[i];
        p3d_fill(q[0], q[1] + o, q[2], q[3] + o, q[4], q[5] + o, sb.dance.tilec[i]);
        p3d_fill(q[0], q[1] + o, q[4], q[5] + o, q[6], q[7] + o, sb.dance.tilec[i]);
        for (k = 0; k < 4; k++)
            cv_line((int32_t)q[2 * k], oy + (int32_t)q[2 * k + 1], (int32_t)q[(2 * k + 2) % 8], oy + (int32_t)q[(2 * k + 3) % 8], RGB(0x2c, 0x3a, 0x90));
    }
    for (i = 0; i < dn.nseg; i++)
        cv_line(sb.dance.seg[i][0], oy + sb.dance.seg[i][1], sb.dance.seg[i][2], oy + sb.dance.seg[i][3], sb.dance.segc[i]);
    p3d_draw(oy);
    for (k = 0; k < 16; k++) {          /* cyan specks drifting in front */
        float x = p3d_hash((uint32_t)k * 9u + 6000u) * 240.0f + 25.0f * t;
        x -= 240.0f * fm_floorf(x / 240.0f);
        cv_rect((int32_t)x, oy + 6 + (int32_t)(p3d_hash((uint32_t)k * 10u + 6100u) * 40.0f), 2, 2, RGB(0x5f, 0xe0, 0xff));
    }
}

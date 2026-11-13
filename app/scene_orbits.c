/* ORBITS' radar (spec ORBITS section 5; docs/screens/orbits.html, V2): round orbits around a faceted planet turning
 * slowly, a tick per step, the gate at the top; each moon a gem at its colour, the way travelled since the gate in
 * dots; a moon passing turns pink, every moon passing together makes the planet flash. A sky of fixed stars and a dim
 * far planet turning in the corner. Part of the unity build after scene_tonnetz.c (app_ui.c): KBLUE, KGREEN, ORANGE,
 * MB_WHITE, PINK3, glo_text. */

#define ORV_CX 104.0f               /* the radar's centre in the scene area */
#define ORV_CY 94.0f               /* (the 4th orbit's gate stays under the top strip) */
#define ORV_PASS_MS 160u
#define ORV_FLASH_MS 300u
#define ORV_ORBIT RGB(0x2a, 0x21, 0x48)
#define ORV_ORBIT_SEL RGB(0x8a, 0x84, 0xab)
#define ORV_TICK RGB(0x4a, 0x44, 0x66)
#define ORV_LINE RGB(0x1c, 0x13, 0x40)
#define ORV_TAG RGB(0x14, 0x0e, 0x30)
#define ORV_TAG_SEL RGB(0xe4, 0xe0, 0xf5)
static const uint16_t ORV_PLANET[6] = {RGB(0x8f, 0xb0, 0xff), RGB(0x6a, 0x8d, 0xff), RGB(0x3a, 0x6d, 0xff),
                                       RGB(0x2a, 0x52, 0xc8), RGB(0x1c, 0x35, 0x90), RGB(0x16, 0x25, 0x5e)};
static const uint16_t ORV_FAR[5] = {RGB(0x2a, 0x21, 0x48), RGB(0x24, 0x18, 0x50), RGB(0x1c, 0x13, 0x40),
                                    RGB(0x16, 0x0f, 0x30), RGB(0x12, 0x0c, 0x2a)};
static const uint16_t ORV_FLASH[6] = {RGB(0xff, 0xff, 0xff), RGB(0xff, 0xff, 0xff), RGB(0xf4, 0xf1, 0xff),
                                      RGB(0xe4, 0xe0, 0xf5), RGB(0xc9, 0xc3, 0xe4), RGB(0xb8, 0xb2, 0xd6)};

static struct {
    int seen;
    uint32_t n, step_t, ms, all_t;
    int hits;                       /* the moons that passed at the step n - 1 */
    int sync;                       /* SYNC: steps to the next step every moon passes at, -1 none within 999 */
    q_orbits o;                     /* the settings SYNC was found for */
} orv;

void orbits_enter(void) { orv.seen = 0; }

static const uint16_t *orv_tones(int m) { return m == 0 ? ORANGE : m == 1 ? KBLUE : m == 2 ? KGREEN : MB_WHITE; }
static float orv_r(int m) { return 22.0f + 15.0f * (float)m; }

/* moon m's place on its orbit: steps since its gate, 0..per (the step's fraction included) */
static float orv_u(const q_orview *v, int m)
{
    float f = v->playing && v->step_ms ? (float)(orv.ms - orv.step_t) / (float)v->step_ms : 0.0f;
    if (f > 1.0f) f = 1.0f;
    int per = v->o.per[m];                      /* in integers first: exact however long the session */
    int k = v->n ? (int)((v->n - 1) % (uint32_t)per) : 0, ph = v->o.pha[m] % per;
    float u = (float)((k - ph + per) % per) + (v->n ? f : 0.0f);
    return u >= (float)per ? u - (float)per : u;
}
static void orv_at(int m, float u, int per, float *x, float *y)
{
    float a = -1.5707963f + u / (float)per * 6.2831853f;
    *x = ORV_CX + orv_r(m) * fm_cosf(a), *y = ORV_CY + orv_r(m) * fm_sinf(a);
}

/* a low-poly sphere turning: 8 meridians x 5 bands, tilted, flat facets toned by the light from the top left */
static void orv_sphere(float cx, float cy, float rad, float turn, const uint16_t *tones, int nt, float z)
{
    float vx[6][8], vy[6][8], vz[6][8];
    const float ct = fm_cosf(0.35f), st = fm_sinf(0.35f);
    for (int a = 0; a <= 5; a++)
        for (int b = 0; b < 8; b++) {
            float th = 3.1415927f * (float)a / 5.0f, ph = 6.2831853f * (float)b / 8.0f + turn;
            float x = fm_sinf(th) * fm_cosf(ph), y = fm_cosf(th), zz = fm_sinf(th) * fm_sinf(ph);
            vx[a][b] = x, vy[a][b] = y * ct - zz * st, vz[a][b] = y * st + zz * ct;
        }
    for (int a = 0; a < 5; a++)
        for (int b = 0; b < 8; b++) {
            int b1 = (b + 1) % 8;
            float mx = (vx[a][b] + vx[a][b1] + vx[a + 1][b] + vx[a + 1][b1]) * 0.25f;
            float my = (vy[a][b] + vy[a][b1] + vy[a + 1][b] + vy[a + 1][b1]) * 0.25f;
            float mz = (vz[a][b] + vz[a][b1] + vz[a + 1][b] + vz[a + 1][b1]) * 0.25f;
            if (mz < 0.0f) continue;                  /* the far side */
            float d = -0.5f * mx + 0.6f * my + 0.62f * mz;
            int k = (int)((1.0f - (d < 0.0f ? 0.0f : d)) * (float)nt);
            uint16_t c = tones[k < nt ? k : nt - 1];
            float x0 = cx + rad * vx[a][b], y0 = cy - rad * vy[a][b], x1 = cx + rad * vx[a][b1], y1 = cy - rad * vy[a][b1];
            float x2 = cx + rad * vx[a + 1][b1], y2 = cy - rad * vy[a + 1][b1], x3 = cx + rad * vx[a + 1][b], y3 = cy - rad * vy[a + 1][b];
            if (a > 0) p3d_tri(x0, y0, x1, y1, x2, y2, z + mz, c);       /* (the poles' quads are triangles) */
            if (a < 4) p3d_tri(x0, y0, x2, y2, x3, y3, z + mz, c);
        }
}

/* v in decimal, 0..999 */
static void orv_num(int v, char *b)
{
    char t[4];
    int n = 0;
    do t[n++] = (char)('0' + v % 10), v /= 10; while (v && n < 3);
    while (n) *b++ = t[--n];
    *b = 0;
}

static void orv_gem(float x, float y, float r, const uint16_t *t, float z)
{
    p3d_tri(x, y - r, x + r, y, x, y, z, t[0]);
    p3d_tri(x + r, y, x, y + r, x, y, z, t[2]);
    p3d_tri(x, y + r, x - r, y, x, y, z, t[2]);
    p3d_tri(x - r, y, x, y - r, x, y, z, t[1]);
}

void orbits_frame(const q_orview *v, uint32_t ms)
{
    if (!orv.seen || v->n != orv.n) {           /* a step: who passed; every moon (at least 2): the planet flashes */
        orv.hits = v->n ? gen_orbits_hits(&v->o, v->n - 1) : 0;
        orv.step_t = orv.seen ? ms : ms - 100000u;
        if (v->playing && v->o.moons > 1 && orv.hits == (1 << v->o.moons) - 1)
            orv.all_t = orv.step_t;
        if (!orv.seen)
            orv.all_t = ms - 100000u;
        orv.n = v->n;
        orv.sync = -2;
    }
    if (orv.sync == -2 || __builtin_memcmp(&orv.o, &v->o, sizeof v->o)) {   /* SYNC, once a step or a change */
        int all = (1 << v->o.moons) - 1, d = v->n ? 1 : 0;   /* stopped: from the first step, which counts */
        uint32_t from = v->n ? v->n - 1 : 0;
        while (d <= 999 && gen_orbits_hits(&v->o, from + (uint32_t)d) != all) d++;
        orv.sync = d > 999 ? -1 : d, orv.o = v->o;
    }
    orv.seen = 1, orv.ms = ms;
    p3d_begin();
    orv_sphere(222.0f, 146.0f, 46.0f, (float)(ms % 56000u) * (6.2831853f / 56000.0f), ORV_FAR, 5, -20.0f);
    for (int m = 0; m < v->o.moons; m++) {      /* the orbits, round */
        uint16_t c = m == v->sel ? ORV_ORBIT_SEL : ORV_ORBIT;
        float px = ORV_CX, py = ORV_CY - orv_r(m);
        for (int s = 1; s <= 32; s++) {
            float a = -1.5707963f + 6.2831853f * (float)s / 32.0f, x = ORV_CX + orv_r(m) * fm_cosf(a), y = ORV_CY + orv_r(m) * fm_sinf(a);
            p3d_line(px, py, x, y, 0.0f, 1, c);
            px = x, py = y;
        }
    }
    int flash = v->playing && ms - orv.all_t < ORV_FLASH_MS && (ms - orv.all_t) / 75u % 2u == 0u;
    orv_sphere(ORV_CX, ORV_CY, 15.0f, (float)(ms % 25000u) * (6.2831853f / 25000.0f), flash ? ORV_FLASH : ORV_PLANET, 6, 10.0f);
    for (int m = 0; m < v->o.moons; m++) {      /* the moons */
        float x, y;
        int hot = v->playing && (orv.hits >> m & 1) && ms - orv.step_t < ORV_PASS_MS;
        orv_at(m, orv_u(v, m), v->o.per[m], &x, &y);
        orv_gem(x, y, hot ? 8.0f : 6.0f, hot ? PINK3 : orv_tones(m), 20.0f);
    }
}

void orbits_band(const q_orview *v, int32_t oy)
{
    int m, i;
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    for (i = 0; i < 40; i++)                    /* the sky */
        cv_rect((int32_t)((i * 733 + 170) / 10 % 240), oy + 4 + (int32_t)((i * 479 + 40) / 10 % 160), 1, 1,
                i % 4 ? RGB(0x1c, 0x13, 0x40) : RGB(0x3a, 0x2f, 0x63));
    for (m = 0; m < v->o.moons; m++) {          /* the dots of the way travelled, under the orbits' lines */
        float u = orv_u(v, m), len = u / (float)v->o.per[m] * 6.2831853f * orv_r(m);
        for (int q = 1; (float)q * 5.0f < len; q++) {
            float x, y;
            orv_at(m, u * (float)q * 5.0f / len, v->o.per[m], &x, &y);
            cv_rect((int32_t)x - 1, oy + (int32_t)y - 1, 2, 2, orv_tones(m)[2]);
        }
    }
    p3d_draw(oy);
    for (m = 0; m < v->o.moons; m++) {
        int per = v->o.per[m], hot = v->playing && (orv.hits >> m & 1) && orv.ms - orv.step_t < ORV_PASS_MS;
        for (i = 0; i < per; i++) {               /* a tick per step, the gate's pink */
            float x, y;
            orv_at(m, (float)i, per, &x, &y);
            uint16_t c = i ? ORV_TICK : PINK3[1];
            cv_rect((int32_t)x - 1, oy + (int32_t)y, 3, 1, c);
            cv_rect((int32_t)x, oy + (int32_t)y - 1, 1, 3, c);
        }
        int32_t gy = oy + (int32_t)(ORV_CY - orv_r(m));   /* the note, at its gate's height */
        cv_rect((int32_t)ORV_CX + 4, gy, 192 - (int32_t)ORV_CX, 1, hot ? PINK3[1] : ORV_LINE);
        glo_text(200, gy, &FONT_XS, v->name[m], hot ? C_WHITE : orv_tones(m)[1], 2);
    }
    cv_rect(0, oy, 240, 20, RGB(0, 0, 0));      /* the top strip: each moon's period, the one set framed; SYNC */
    for (m = 0; m < v->o.moons; m++) {
        int32_t x = 8 + 34 * m;
        cv_rect(x, oy + 4, 30, 12, m == v->sel ? ORV_TAG_SEL : ORV_TAG);
        const uint16_t *t = orv_tones(m);
        p3d_fill((float)(x + 7), (float)(oy + 6), (float)(x + 11), (float)(oy + 10), (float)(x + 3), (float)(oy + 10), t[0]);
        p3d_fill((float)(x + 11), (float)(oy + 10), (float)(x + 7), (float)(oy + 14), (float)(x + 3), (float)(oy + 10), t[2]);
        char b[4];
        orv_num(v->o.per[m], b);
        glo_text(x + 21, oy + 10, &FONT_XS, b, m == v->sel ? RGB(0, 0, 0) : RGB(0xc9, 0xc3, 0xe4), 0);
    }
    char s[10] = {'S', 'Y', 'N', 'C', ' ', 0};  /* SYNC (orbits_frame), "-" if never within 999 steps */
    if (orv.sync < 0) s[5] = '-', s[6] = 0;
    else orv_num(orv.sync, s + 5);
    glo_text(234, oy + 10, &FONT_XS, s, v->playing && orv.ms - orv.all_t < ORV_FLASH_MS ? PINK3[1] : RGB(0x4a, 0x44, 0x66), 1);
}

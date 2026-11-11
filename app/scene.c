/* The FM planet (spec 9.3, docs/screens/scenes.html "6-OP FM"): a cut planet with a ring, ships in orbit that
 * fire at it on every note, over a grid floor under a starfield. Part of the unity build (app.c), after p3d.c.
 * Everything moves with the clock, not the frame count: a slower frame shows the same scene, later. */
#include "scene.h"
#include "p3d.h"
#include "fastmath.h"

#define CX 120.0f                   /* the planet's centre in the scene area */
#define CY 62.0f
#define R 26.0f
#define HORIZON 120

static const uint16_t ORANGE[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)};
static const uint16_t SHIP[2][3] = {{RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)},
                                    {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)}};
static const uint16_t MB_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const uint16_t PINK3[3] = {RGB(0xff, 0xb8, 0xdc), RGB(0xff, 0x2f, 0x8e), RGB(0x8a, 0x10, 0x50)};
#define C_SKY RGB(0x12, 0x0b, 0x26)
#define C_FLOOR RGB(0x1a, 0x0f, 0x33)
#define C_GRID RGB(0x5b, 0x2f, 0x8c)
#define C_STAR RGB(0xff, 0xff, 0xff)
#define C_STAR2 RGB(0x6c, 0x5f, 0xa0)
#define C_SHADOW RGB(0x0b, 0x06, 0x18)

static p3_vec planet_v[60];
static p3_face planet_f[80];
static p3_mesh planet;
static struct { uint32_t hits, hit_ms; int seen; float t; } sc;

void scene_enter(void) { sc.seen = 0; }

/* n stars scattered at random over rows y0 .. y0 + h, fixed places, drifting left: the big ones few and faster
   (scenes.c, plateau.c) */
static void stars(int32_t oy, float t, int n, int y0, int h, float speed)
{
    for (int i = 0; i < n; i++) {
        int layer = p3d_hash((uint32_t)i * 3u + 1u) < 0.15f ? 2 : p3d_hash((uint32_t)i * 3u + 2u) < 0.4f ? 1 : 0;
        float x = p3d_hash((uint32_t)i * 7u + 11u) * 240.0f - t * (3.0f + 4.0f * (float)layer) * speed;
        x -= 240.0f * fm_floorf(x / 240.0f);
        cv_rect((int32_t)x, oy + y0 + (int32_t)(p3d_hash((uint32_t)i * 13u + 5u) * (float)h), layer == 2 ? 2 : 1, layer == 2 ? 2 : 1,
                layer == 2 ? C_STAR : layer ? RGB(0xa9, 0x9c, 0xe0) : RGB(0x4a, 0x3f, 0x78));
    }
}

int scene_has(int model) { return model == 2 || model == 3 || model == 4 || model == 10; }

/* a point of the orbit plane, tilted towards the viewer: the far side (depth < 0) is higher on the screen */
static void orbit(float a, float r, float tilt_s, float tilt_c, float *x, float *y, float *z)
{
    float s = fm_sinf(a);
    *x = CX + r * fm_cosf(a);
    *y = CY + r * s * tilt_s;
    *z = r * s * tilt_c;
}

void scene_frame(const int16_t knobs[4], uint32_t hits, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f;    /* wrapped hourly: the angles stay small for fm_sinf */
    float tilt = 0.15f + 1.0f * (float)knobs[2] / 127.0f, ts = fm_sinf(tilt), tc = fm_cosf(tilt);
    float h, gap = 8.0f + 10.0f * (float)knobs[1] / 127.0f;
    int i, n = 2 + (3 * knobs[0] + 63) / 127;
    p3_pose pose = {0.6f * t, 0.0f, R, CX, CY};
    if (!planet.nv)
        p3d_sphere(5, 10, planet_v, planet_f, &planet);
    if (!sc.seen || hits != sc.hits) {           /* a note since the last frame (the first frame flashes nothing) */
        sc.hit_ms = sc.seen ? ms : ms - 100000u;
        sc.hits = hits;
        sc.seen = 1;
    }
    h = fm_expf(-(float)(ms - sc.hit_ms) / (50.0f + 250.0f * (float)knobs[3] / 127.0f));
    sc.t = t;
    p3d_begin();
    p3d_tri(CX - 34, 138, CX + 34, 138, CX + 22, 144, -1000.0f, C_SHADOW);   /* the planet's shadow on the floor */
    p3d_tri(CX - 34, 138, CX + 22, 144, CX - 22, 144, -1000.0f, C_SHADOW);
    p3d_mesh(&planet, &pose, h > 0.5f ? PINK3 : ORANGE);
    for (i = 0; i < 12; i++) {                   /* the ring: 12 straight segments, 4 px thick */
        float x0, y0, z0, x1, y1, z1, nx, ny, l;
        orbit((float)i * (FM_TWO_PI / 12.0f), 1.85f * R, ts, tc, &x0, &y0, &z0);
        orbit((float)(i + 1) * (FM_TWO_PI / 12.0f), 1.85f * R, ts, tc, &x1, &y1, &z1);
        l = fm_sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        nx = -(y1 - y0) / l * 2.0f;
        ny = (x1 - x0) / l * 2.0f;
        uint16_t c = z0 + z1 > 0.0f ? ORANGE[0] : ORANGE[2];
        p3d_tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, 0.5f * (z0 + z1), c);
        p3d_tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, 0.5f * (z0 + z1), c);
    }
    for (i = 0; i < n; i++) {                    /* the ships, each on its own orbit and speed */
        float a = t * 1.4f / (1.0f + 0.5f * (float)i) + 2.1f * (float)i, x, y, z;
        float dx = -fm_sinf(a), dy = fm_cosf(a) * ts, l = fm_sqrtf(dx * dx + dy * dy);
        const uint16_t *k = SHIP[i % 2];
        orbit(a, 42.0f + (float)i * gap, ts, tc, &x, &y, &z);
        dx /= l;
        dy /= l;
#define SX(lx, ly) (x + dx * (lx) - dy * (ly))
#define SY(lx, ly) (y + dy * (lx) + dx * (ly))
        p3d_tri(SX(8, 0), SY(8, 0), SX(-5, 5), SY(-5, 5), SX(-2, 0), SY(-2, 0), z, k[1]);
        p3d_tri(SX(8, 0), SY(8, 0), SX(-2, 0), SY(-2, 0), SX(-5, -5), SY(-5, -5), z, k[0]);
        p3d_tri(SX(-5, 3), SY(-5, 3), SX(-10, 0), SY(-10, 0), SX(-5, -3), SY(-5, -3), z, PINK3[1]);
#undef SX
#undef SY
        if (h > 0.35f)                           /* they fire at the planet */
            p3d_line(x, y, x + 0.75f * (CX - x), y + 0.75f * (CY - y), 1000.0f, 2, PINK3[0]);
    }
}

void scene_band(int32_t oy)
{
    float t = sc.t, f = 0.6f * t - fm_floorf(0.6f * t);
    int i;
    cv_rect(0, oy, 240, HORIZON, C_SKY);
    cv_rect(0, oy + HORIZON, 240, 166 - HORIZON, C_FLOOR);
    stars(oy, t, 30, 4, 100, 1.0f);
    cv_rect(0, oy + HORIZON, 240, 1, C_GRID);
    for (i = -10; i <= 10; i++)                  /* the floor's vanishing lines */
        cv_line(120 + 10 * i, oy + HORIZON, 120 + 70 * i, oy + 166, C_GRID);
    for (i = 0; i < 6; i++) {                    /* and its rows, coming towards the viewer */
        float p = ((float)i + f) / 6.0f;
        p = p > 0.0f ? fm_powf(p, 2.2f) : 0.0f;
        cv_rect(0, oy + HORIZON + (int32_t)(46.0f * p), 240, 1, C_GRID);
    }
    p3d_draw(oy);
}

/* ---- MARBLES: the fleet (spec 5b, 3; docs/screens/marbles.html) --------------------------------------------- */
#define MB_SHOTS 6                  /* shots in flight */
#define MB_FALL 500u                /* a shot flies a step, this at most */
#define MB_DECAY 4000u              /* a crystal's history loses one this often */
#define MB_HZ 66                    /* the horizon in the scene area */
#define MB_CY 138                   /* the crystals' floor */
#define MB_NROCK 6                  /* depths of rocks (fewer if the 256 primitives ask, the user agreed) */
static const uint16_t ROCK[3] = {RGB(0x6a, 0x48, 0xb0), RGB(0x46, 0x30, 0x7e), RGB(0x26, 0x18, 0x4e)};
static const uint16_t PED[3] = {RGB(0x3a, 0x2c, 0x64), RGB(0x2a, 0x1d, 0x55), RGB(0x1a, 0x14, 0x38)};
static const char *const MB_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* MODE: the formation, each ship's place from the leader in units of SPREAD (6: a ring, computed) */
static const int8_t FORM[7][3][2] = {{{0, 0}, {-10, 3}, {10, 3}}, {{0, 0}, {-10, 7}, {10, 7}}, {{0, 0}, {0, 8}, {0, 16}},
                                     {{0, 0}, {-8, 5}, {-16, 10}}, {{0, 0}, {-10, 4}, {10, -4}}, {{0, 6}, {-10, 0}, {10, 0}}, {{0}}};

static p3_vec ico_v[12], spire_v[6][10], boulder_v[6][12];
static p3_face ico_f[20], spire_f[13];
static p3_mesh ico, boulder[6], spire[3];
static p3_vec crys_v[6];
static p3_mesh crystal;
static struct {
    struct { uint8_t on, note, ship; uint32_t fire, land; } m[MB_SHOTS];
    uint8_t hist[12], next_ship;
    uint32_t hit[12], fired[3], decay_ms, ms, last;
    float d;                        /* the canyon's run so far, in rock depths */
    q_mview v;
    int started, ships;
} mb;

static void face_out(p3_face *f, const p3_vec *v)   /* turn a face to face out of a mesh around (0, -0.5, 0) */
{
    const p3_vec *a = &v[f->a], *b = &v[f->b], *c = &v[f->c];
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
    float d = (uy * wz - uz * wy) * (a->x + b->x + c->x) + (uz * wx - ux * wz) * (a->y + b->y + c->y + 1.5f) +
              (ux * wy - uy * wx) * (a->z + b->z + c->z);
    if (d < 0.0f) {
        uint8_t t = f->b;
        f->b = f->c;
        f->c = t;
    }
}

static void meshes_build(void)
{
    static const int8_t V[12][3] = {{-1, 2, 0}, {1, 2, 0}, {-1, -2, 0}, {1, -2, 0}, {0, -1, 2}, {0, 1, 2},
                                    {0, -1, -2}, {0, 1, -2}, {2, 0, -1}, {2, 0, 1}, {-2, 0, -1}, {-2, 0, 1}};   /* 2: the golden ratio */
    static const uint8_t F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                     {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                     {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    const float phi = 1.618034f, n = 1.0f / fm_sqrtf(1.0f + phi * phi);
    int i, k;
    for (i = 0; i < 12; i++) {
        float c[3];
        for (k = 0; k < 3; k++)
            c[k] = V[i][k] == 2 ? phi : V[i][k] == -2 ? -phi : (float)V[i][k];
        ico_v[i] = (p3_vec){c[0] * n, c[1] * n, c[2] * n};
    }
    for (i = 0; i < 20; i++) {
        ico_f[i] = (p3_face){F[i][0], F[i][1], F[i][2]};
        face_out(&ico_f[i], ico_v);  /* (centred: the 1.5 shifts the centre, harmless for a sphere around 0) */
    }
    ico = (p3_mesh){ico_v, ico_f, 12, 20};
    for (k = 0; k < 6; k++) {       /* boulders: a bumpy icosahedron, its bottom flattened to stand on y = 0 */
        for (i = 0; i < 12; i++) {
            p3_vec q = ico_v[i];
            float j = 0.35f, y = q.y - 0.55f;
            boulder_v[k][i] = (p3_vec){1.1f * q.x + (p3d_hash((uint32_t)(k * 97 + i * 7)) - 0.5f) * j,
                                       1.1f * (y < 0.0f ? y : 0.0f) + (p3d_hash((uint32_t)(k * 97 + i * 7 + 1)) - 0.5f) * j,
                                       q.z + (p3d_hash((uint32_t)(k * 97 + i * 7 + 2)) - 0.5f) * j};
        }
        boulder[k] = (p3_mesh){boulder_v[k], ico_f, 12, 20};
    }
    for (k = 0; k < 3; k++) {       /* spires: a 5-sided column narrowing to 0.18 at 3.2 high */
        for (i = 0; i < 5; i++) {
            float a = (float)i * (FM_TWO_PI / 5.0f) + p3d_hash((uint32_t)(k * 31 + i)) * 0.5f, r = 0.85f + p3d_hash((uint32_t)(k * 31 + i + 9)) * 0.3f;
            spire_v[k][2 * i] = (p3_vec){fm_cosf(a) * r, 0.0f, fm_sinf(a) * r};
            spire_v[k][2 * i + 1] = (p3_vec){fm_cosf(a) * r * 0.18f, -3.2f, fm_sinf(a) * r * 0.18f};
        }
        spire[k] = (p3_mesh){spire_v[k], spire_f, 10, 13};
    }
    for (i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        spire_f[2 * i] = (p3_face){(uint8_t)(2 * i), (uint8_t)(2 * j), (uint8_t)(2 * j + 1)};
        spire_f[2 * i + 1] = (p3_face){(uint8_t)(2 * i), (uint8_t)(2 * j + 1), (uint8_t)(2 * i + 1)};
    }
    spire_f[10] = (p3_face){1, 3, 5};
    spire_f[11] = (p3_face){1, 5, 7};
    spire_f[12] = (p3_face){1, 7, 9};
    for (i = 0; i < 13; i++)
        face_out(&spire_f[i], spire_v[0]);
    static const p3_face CF[8] = {{0, 4, 2}, {4, 1, 2}, {1, 5, 2}, {5, 0, 2}, {4, 0, 3}, {1, 4, 3}, {5, 1, 3}, {0, 5, 3}};
    static p3_face cf[8];
    crys_v[0] = (p3_vec){0.7f, 0, 0}, crys_v[1] = (p3_vec){-0.7f, 0, 0}, crys_v[2] = (p3_vec){0, 1.4f, 0};
    crys_v[3] = (p3_vec){0, -1.4f, 0}, crys_v[4] = (p3_vec){0, 0, 0.7f}, crys_v[5] = (p3_vec){0, 0, -0.7f};
    for (i = 0; i < 8; i++) {
        cf[i] = CF[i];
        const p3_vec *a = &crys_v[cf[i].a], *b = &crys_v[cf[i].b], *c = &crys_v[cf[i].c];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
        if ((uy * wz - uz * wy) * (a->x + b->x + c->x) + (uz * wx - ux * wz) * (a->y + b->y + c->y) + (ux * wy - uy * wx) * (a->z + b->z + c->z) < 0.0f) {
            uint8_t t = cf[i].b;
            cf[i].b = cf[i].c;
            cf[i].c = t;
        }
    }
    crystal = (p3_mesh){crys_v, cf, 6, 8};
}

int marbles_bin(uint16_t allowed, int note, int *nbins)
{
    int pc = note % 12, n = 0, best = 0, bd = 99;
    for (int c = 0; c < 12; c++)
        if (allowed >> c & 1u) {
            int d = c > pc ? c - pc : pc - c;
            if (12 - d < d)
                d = 12 - d;
            if (d < bd)                 /* a tie keeps the lower bin */
                bd = d, best = n;
            n++;
        }
    *nbins = n;
    return best;
}

int marbles_flying(void)
{
    int n = 0;
    for (int i = 0; i < MB_SHOTS; i++)
        n += mb.m[i].on;
    return n;
}

int marbles_hist(int bin) { return mb.hist[bin]; }
int marbles_ships(void) { return mb.ships; }

void marbles_enter(void)
{
    for (int i = 0; i < MB_SHOTS; i++)
        mb.m[i].on = 0;
}

void marbles_drop(int note, uint32_t ms, uint32_t land_ms)
{
    int i, k = 0;
    for (i = 0; i < MB_SHOTS; i++) {    /* a free slot, else the shot landing first */
        if (!mb.m[i].on) {
            k = i;
            break;
        }
        if (mb.m[i].land - mb.m[k].land > 0x80000000u)
            k = i;
    }
    mb.m[k].on = 1;
    mb.m[k].note = (uint8_t)note;
    mb.m[k].land = land_ms;
    mb.m[k].fire = land_ms - ms > MB_FALL ? land_ms - MB_FALL : ms;   /* it leaves a step before, half a second at most */
    mb.m[k].ship = mb.next_ship;        /* the ships fire in turn */
    mb.next_ship = (uint8_t)((mb.next_ship + 1) % (mb.v.tout ? mb.v.tout : 1));
}

static float crystal_x(int i, int n) { return n < 2 ? 120.0f : 28.0f + (float)i * 184.0f / (float)(n - 1); }

/* a ship's place at ms: the leader's loop, the formation, the sway */
static void ship_at(int ch, uint32_t ms, float *x, float *y)
{
    const q_mview *v = &mb.v;
    float tt = (float)(ms % 3600000u) * 0.001f, ph = tt * 0.5f * 250.0f / (float)v->step_ms, gap = (16.0f + (float)v->spread / 127.0f * 26.0f) / 10.0f;
    float ox, oy, sway = (float)v->jitter / 127.0f * 1.2f;
    if (v->mode == 6) {
        float a = tt * 2.0f + (float)ch * (FM_TWO_PI / 3.0f);
        ox = fm_cosf(a) * 8.0f * gap, oy = fm_sinf(a) * 4.0f * gap;
    } else {
        ox = (float)FORM[v->mode][ch][0] * gap, oy = (float)FORM[v->mode][ch][1] * gap * 0.6f;
    }
    *x = 120.0f + ((float)v->xbias - 64.0f) / 64.0f * 55.0f + fm_sinf(ph) * 46.0f + ox + sway * fm_sinf(tt * 5.0f + (float)ch * 2.1f);
    *y = 42.0f + fm_sinf(2.0f * ph) * 14.0f + oy + sway * fm_sinf(tt * 5.0f + 0.3f + (float)ch * 2.1f);
}

static void ship_draw(float x, float y, float c, float n, float bank, const uint16_t *T, float s, int flame)   /* (c, n): its heading */
{
#define SX(dx, dy) (x + (c * (dx) - n * (dy) * bank) * s)
#define SY(dx, dy) (y + (n * (dx) + c * (dy) * bank) * s)
    p3d_tri(SX(-6, 0), SY(-6, 0), SX(-11, 2.5f), SY(-11, 2.5f), SX(-9 - flame * 6, 0), SY(-9 - flame * 6, 0), 500.0f, flame ? PINK3[0] : PINK3[1]);
    p3d_tri(SX(-6, 0), SY(-6, 0), SX(-9 - flame * 6, 0), SY(-9 - flame * 6, 0), SX(-11, -2.5f), SY(-11, -2.5f), 500.0f, flame ? PINK3[0] : PINK3[1]);
    p3d_tri(SX(10, 0), SY(10, 0), SX(-6, 7), SY(-6, 7), SX(-3, 0), SY(-3, 0), 501.0f, T[1]);
    p3d_tri(SX(10, 0), SY(10, 0), SX(-3, 0), SY(-3, 0), SX(-6, -7), SY(-6, -7), 501.0f, T[0]);
    p3d_tri(SX(4, 0), SY(4, 0), SX(-3, 2), SY(-3, 2), SX(-3, -2), SY(-3, -2), 502.0f, T[2]);
#undef SX
#undef SY
}

/* an isometric box standing on (x, y), half sizes sx, sy, height h, in the list at depth z */
static void box3(float x, float y, float sx, float sy, float h, const uint16_t *T, float z)
{
    p3d_tri(x - sx, y, x, y + sy, x, y + sy - h, z, T[1]);
    p3d_tri(x - sx, y, x, y + sy - h, x - sx, y - h, z, T[1]);
    p3d_tri(x, y + sy, x + sx, y, x + sx, y - h, z, T[2]);
    p3d_tri(x, y + sy, x + sx, y - h, x, y + sy - h, z, T[2]);
    p3d_tri(x, y - sy - h, x + sx, y - h, x, y + sy - h, z + 0.5f, T[0]);
    p3d_tri(x, y - sy - h, x, y + sy - h, x - sx, y - h, z + 0.5f, T[0]);
}

void marbles_frame(const q_mview *v, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f, f;
    int i, n, b, deja = v->dejavu > 64;
    uint32_t base;
    if (!ico.nv)
        meshes_build();
    if (!mb.started || v->allowed != mb.v.allowed) {   /* other notes allowed: the history starts again */
        __builtin_memset(mb.hist, 0, sizeof mb.hist);
        mb.decay_ms = mb.last = ms;
        mb.started = 1;
    }
    while (ms - mb.decay_ms >= MB_DECAY) {
        mb.decay_ms += MB_DECAY;
        for (i = 0; i < 12; i++)
            mb.hist[i] -= mb.hist[i] > 0;
    }
    if (ms - mb.last < 500u)            /* the canyon runs at DIV's pace: integrated, so DIV changes the pace, not the place */
        mb.d += (float)(ms - mb.last) * 0.5f * 0.25f / (float)v->step_ms;
    mb.last = ms;
    mb.v = *v;
    mb.ms = ms;
    mb.ships = v->tout < 1 ? 1 : v->tout > 3 ? 3 : v->tout;
    p3d_begin();
    /* the rocks: two lanes a side, stones on the floor; a far one is a single triangle */
    f = mb.d - fm_floorf(mb.d);
    base = (uint32_t)(int32_t)fm_floorf(mb.d);
    for (int k = 0; k < MB_NROCK; k++) {
        uint32_t id = base - (uint32_t)k;
        float p = 0.72f * fm_powf(((float)k + f) / (float)MB_NROCK, 1.9f), y = (float)MB_HZ + p * 96.0f;
        for (int side = -1; side <= 1; side += 2)
            for (int lane = 0; lane < 2; lane++) {
                uint32_t r0 = id * 23u + (side > 0 ? 11u : 0u) + (uint32_t)lane * 5u;
                if (p3d_hash(r0) < (lane ? 0.2f : 0.65f))
                    continue;           /* a gap */
                float out = lane ? 120.0f + p3d_hash(r0 + 62u) * 60.0f : 55.0f + p3d_hash(r0 + 62u) * 35.0f;
                float x = 120.0f + (float)side * (10.0f + p * out * 1.5f + fm_powf(p / 0.72f, 4.0f) * 140.0f);
                float sc = p * (lane ? 26.0f + p3d_hash(r0 + 93u) * 16.0f : 12.0f + p3d_hash(r0 + 93u) * 10.0f);
                if (x < -40.0f || x > 280.0f)
                    continue;
                if (p < 0.15f) {        /* far: a single triangle */
                    p3d_tri(x - sc, y, x, y - sc * 1.4f, x + sc, y, -2000.0f + p * 2000.0f, ROCK[1]);
                    continue;
                }
                const p3_mesh *m = p3d_hash(r0 * 5u) > 0.7f ? &spire[r0 % 3u] : &boulder[r0 % 6u];
                p3_pose pose = {p3d_hash(r0 + 124u) * FM_TWO_PI, 0.3f, sc, x, y, -2000.0f + p * 2000.0f};
                p3d_mesh(m, &pose, ROCK);
            }
        if (p3d_hash(id * 41u + 7u) < 0.55f && p >= 0.15f) {   /* a stone in the middle */
            float off = (p3d_hash(id * 41u + 20u) - 0.5f) * 96.0f, sc = p * (5.0f + p3d_hash(id * 41u + 33u) * 7.0f);
            p3_pose pose = {p3d_hash(id * 41u + 46u) * FM_TWO_PI, 0.3f, sc, 120.0f + off * (0.2f + p * 2.2f), y, -2000.0f + p * 2000.0f};
            p3d_mesh(&boulder[id % 6u], &pose, ROCK);
        }
    }
    /* the crystals on their pedestals */
    marbles_bin(v->allowed, 0, &n);
    for (i = 0, b = 0; b < 12; b++) {
        if (!(v->allowed >> b & 1u))
            continue;
        uint32_t h = ms - mb.hit[i];
        int on = v->kept >> b & 1u;
        float x = crystal_x(i, n), ped = on ? 4.0f + (float)mb.hist[i] * 1.5f : 2.0f;
        const uint16_t *T = deja ? ORANGE : SHIP[0];
        box3(x, (float)MB_CY + 4.0f, 9.0f, 4.5f, ped, on ? PED : PED, 10.0f);
        if (on) {
            float grow = h < 100u ? 0.0f : h < 450u ? (float)(h - 100u) / 350.0f : 1.0f, sz = (8.0f + (float)mb.hist[i] * 0.8f) * grow;
            if (sz > 0.5f) {
                p3_pose pose = {t * 1.4f + (float)i, 0.15f, sz, x, (float)MB_CY - ped - 11.0f, 20.0f};
                p3d_mesh(&crystal, &pose, h < 450u ? PINK3 : T);
            }
            if (h < 450u) {             /* shards flying out and falling back */
                static const int8_t SH[6][2] = {{-12, -22}, {14, -26}, {-4, -30}, {22, -14}, {-24, -12}, {8, -18}};
                float k = (float)h / 450.0f;
                for (int s2 = 0; s2 < 6; s2++) {
                    float px = x + (float)SH[s2][0] * 1.4f * k, py = (float)MB_CY - ped - 11.0f + (float)SH[s2][1] * 1.4f * k + 40.0f * k * k, z = 3.5f * (1.0f - k) + 1.0f;
                    p3d_tri(px, py - z, px + z, py + z * 0.4f, px - z * 0.6f, py + z, 30.0f, s2 % 3 == 2 ? PINK3[0] : T[s2 % 3]);
                }
            }
        }
        i++;
    }
    /* the fleet */
    for (int ch = mb.ships - 1; ch >= 0; ch--) {
        float x, y, x2, y2;
        ship_at(ch, ms, &x, &y);
        ship_at(ch, ms + 40u, &x2, &y2);
        float l = fm_sqrtf((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y)), hc = l > 0.0f ? (x2 - x) / l : 1.0f, hs = l > 0.0f ? (y2 - y) / l : 0.0f;
        float bank = 0.55f + 0.45f * fm_fabsf(hc);
        float size = 1.05f + 0.6f * (ch == 0 ? (float)v->bias / 127.0f : 1.0f - (float)v->bias / 127.0f);
        if (deja)                       /* a trail behind it */
            for (int k = 1; k < 10; k++) {
                float ax, ay, bx, by;
                ship_at(ch, ms - (uint32_t)(k * 70), &ax, &ay);
                ship_at(ch, ms - (uint32_t)((k + 1) * 70), &bx, &by);
                p3d_line(ax, ay, bx, by, 400.0f, 1, k < 4 ? ORANGE[1] : ORANGE[2]);
            }
        ship_draw(x, y, hc, hs, bank, deja ? ORANGE : ms - mb.fired[ch] < 60u ? MB_WHITE : SHIP[ch % 2], size, (ms / 33u + (uint32_t)ch) & 1u);
    }
    /* the shots: from the ship where it fired to the crystal, landing when the note sounds */
    for (i = 0; i < MB_SHOTS; i++) {
        if (!mb.m[i].on)
            continue;
        b = marbles_bin(v->allowed, mb.m[i].note, &n);
        if (!n) {
            mb.m[i].on = 0;
            continue;
        }
        if ((int32_t)(ms - mb.m[i].land) >= 0) {   /* landed: its note sounds */
            mb.m[i].on = 0;
            mb.hist[b] += mb.hist[b] < 6;
            mb.hit[b] = ms;
            continue;
        }
        if ((int32_t)(ms - mb.m[i].fire) < 0)
            continue;
        if (ms - mb.m[i].fire < 60u)
            mb.fired[mb.m[i].ship % 3] = mb.m[i].fire;
        float x0, y0, x1 = crystal_x(b, n), y1 = (float)MB_CY - 14.0f, u = (float)(ms - mb.m[i].fire) / (float)(mb.m[i].land - mb.m[i].fire);
        ship_at(mb.m[i].ship % 3, mb.m[i].fire, &x0, &y0);
        static const float LEN[3] = {0.2f, 0.11f, 0.05f};
        for (int k = 0; k < 3; k++) {
            float k0 = u - LEN[k] > 0.0f ? u - LEN[k] : 0.0f;
            p3d_line(x0 + (x1 - x0) * k0, y0 + (y1 - y0) * k0, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, 1000.0f + (float)k, (uint8_t)(k == 2 ? 2 : 1),
                     deja ? ORANGE[2 - k] : PINK3[2 - k]);
        }
    }
}

void marbles_band(int32_t oy)
{
    float t = (float)(mb.ms % 3600000u) * 0.001f, f = mb.d - fm_floorf(mb.d), ridge = mb.d * 0.6f * 8.0f;
    int i, n, k, deja = mb.v.dejavu > 64;
    cv_rect(0, oy, 240, 166, RGB(0x0c, 0x08, 0x20));
    stars(oy, t, 30, 4, 56, 250.0f / (float)mb.v.step_ms * 0.25f);
    for (i = -1; i < 8; i++) {          /* the far ridge, sliding slowly */
        float sh = ridge - 40.0f * fm_floorf(ridge / 40.0f), x0 = (float)(i * 40) - sh;
        uint32_t id = (uint32_t)(int32_t)fm_floorf(ridge / 40.0f) + (uint32_t)i;
        float h = 10.0f + p3d_hash(id * 3u) * 18.0f, px = x0 + 10.0f + p3d_hash(id * 5u) * 20.0f, o = (float)oy;
        p3d_fill(x0 - 6.0f, MB_HZ + o, px, MB_HZ - h + o, px + 4.0f, MB_HZ + o, RGB(0x2a, 0x1c, 0x52));
        p3d_fill(px, MB_HZ - h + o, x0 + 46.0f, MB_HZ + o, px + 4.0f, MB_HZ + o, RGB(0x1c, 0x12, 0x40));
    }
    cv_rect(0, oy + MB_HZ, 240, 166 - MB_HZ, RGB(0x12, 0x0a, 0x28));
    for (k = 0; k < 5; k++) {           /* the canyon floor's lines coming closer */
        float p = ((float)k + f) / 5.0f, y = MB_HZ + p * p * 100.0f, w = 20.0f + p * p * 150.0f;
        cv_rect((int32_t)(120.0f - w), oy + (int32_t)y, (int32_t)(2.0f * w), 1, RGB(0x1e, 0x12, 0x40));
    }
    cv_rect(0, oy + MB_HZ, 240, 1, RGB(0x2e, 0x1c, 0x5a));
    marbles_bin(mb.v.allowed, 0, &n);
    for (i = 0, k = 0; k < 12; k++) {   /* a ring on the floor under a crystal just hit */
        if (!(mb.v.allowed >> k & 1u))
            continue;
        uint32_t h = mb.ms - mb.hit[i];
        if (h < 500u) {
            float x = crystal_x(i, n), g = 6.0f + (float)h / 500.0f * 34.0f, y = (float)MB_CY + 4.0f + (float)oy;
            uint16_t c = h < 200u ? PINK3[1] : PINK3[2];
            cv_line((int32_t)(x - g), (int32_t)y, (int32_t)x, (int32_t)(y - g / 2), c);
            cv_line((int32_t)x, (int32_t)(y - g / 2), (int32_t)(x + g), (int32_t)y, c);
            cv_line((int32_t)(x + g), (int32_t)y, (int32_t)x, (int32_t)(y + g / 2), c);
            cv_line((int32_t)x, (int32_t)(y + g / 2), (int32_t)(x - g), (int32_t)y, c);
        }
        i++;
    }
    p3d_draw(oy);
    for (i = 0, k = 0; k < 12; k++) {   /* the notes' names, the dropped ones as outlines */
        if (!(mb.v.allowed >> k & 1u))
            continue;
        int on = mb.v.kept >> k & 1u, hot = mb.ms - mb.hit[i] < 150u;
        float x = crystal_x(i, n);
        if (!on) {
            float ped = 2.0f, cy = (float)MB_CY - ped - 11.0f + (float)oy;
            cv_line((int32_t)x, (int32_t)(cy - 8.0f), (int32_t)(x + 4.0f), (int32_t)cy, RGB(0x2a, 0x20, 0x50));
            cv_line((int32_t)(x + 4.0f), (int32_t)cy, (int32_t)x, (int32_t)(cy + 8.0f), RGB(0x2a, 0x20, 0x50));
            cv_line((int32_t)x, (int32_t)(cy + 8.0f), (int32_t)(x - 4.0f), (int32_t)cy, RGB(0x2a, 0x20, 0x50));
            cv_line((int32_t)(x - 4.0f), (int32_t)cy, (int32_t)x, (int32_t)(cy - 8.0f), RGB(0x2a, 0x20, 0x50));
        }
        cv_text((int32_t)x - text_w(&FONT_S, MB_NAME[k]) / 2, oy + MB_CY + 10, &FONT_S, MB_NAME[k],
                hot ? PINK3[0] : on ? RGB(0xf4, 0xf1, 0xff) : RGB(0x4a, 0x44, 0x66));
        i++;
    }
    {                                   /* the loop's lights: LEN dashes, the step playing lit */
        int len = mb.v.len < 1 ? 1 : mb.v.len;
        float g = 160.0f / (float)len < 12.0f ? 160.0f / (float)len : 12.0f, x0 = 40.0f + (160.0f - g * (float)len) / 2.0f, o = (float)oy;
        for (i = 0; i < len; i++) {
            float x = x0 + (float)i * g;
            uint16_t c = i == mb.v.pos ? (deja ? ORANGE[0] : SHIP[1][0]) : RGB(0x2a, 0x20, 0x50);
            p3d_fill(x + 1, 10 + o, x + g - 1, 10 + o, x + g - 3, 14 + o, c);
            p3d_fill(x + 1, 10 + o, x + g - 3, 14 + o, x - 1, 14 + o, c);
        }
    }
}

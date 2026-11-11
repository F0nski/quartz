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

/* ---- MARBLES ---------------------------------------------------------------------------------------------- */
#define MB_MAX 6                    /* marbles in flight */
#define MB_FALL 500u                /* ms from the top to the bin */
#define MB_FLASH 150u
#define MB_DECAY 4000u              /* a bin's history loses one this often */
#define MB_FLOOR 146                /* the bins' base */
#define C_MB_BG RGB(0x0c, 0x08, 0x20)
#define C_PIN RGB(0x3a, 0x2c, 0x64)
#define C_BASE RGB(0x2a, 0x20, 0x50)
#define C_MB_TEXT RGB(0xf4, 0xf1, 0xff)
#define C_MB_GRAY RGB(0x8b, 0x84, 0xa8)
static const uint16_t MB_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const char *const MB_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static p3_vec ico_v[12];
static p3_face ico_f[20];
static p3_mesh ico;
static struct {
    struct { uint8_t on, note; uint32_t land; } m[MB_MAX];   /* land: when its note sounds */
    uint8_t hist[12], knobs[3];
    uint32_t flash[12], decay_ms, ms;
    uint16_t allowed;
    int started;
    char top_l[12], top_r[24];
} mb;

static void ico_build(void)
{
    static const int8_t V[12][3] = {{-1, 2, 0}, {1, 2, 0}, {-1, -2, 0}, {1, -2, 0}, {0, -1, 2}, {0, 1, 2},
                                    {0, -1, -2}, {0, 1, -2}, {2, 0, -1}, {2, 0, 1}, {-2, 0, -1}, {-2, 0, 1}};   /* 2: the golden ratio */
    static const uint8_t F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                     {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                     {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    const float phi = 1.618034f, n = 1.0f / fm_sqrtf(1.0f + phi * phi);
    int i;
    for (i = 0; i < 12; i++) {
        float c[3];
        for (int k = 0; k < 3; k++)
            c[k] = V[i][k] == 2 ? phi : V[i][k] == -2 ? -phi : (float)V[i][k];
        ico_v[i] = (p3_vec){c[0] * n, c[1] * n, c[2] * n};
    }
    for (i = 0; i < 20; i++) {          /* each face turned to face out */
        const p3_vec *a = &ico_v[F[i][0]], *b = &ico_v[F[i][1]], *c = &ico_v[F[i][2]];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
        float d = (uy * vz - uz * vy) * (a->x + b->x + c->x) + (uz * vx - ux * vz) * (a->y + b->y + c->y) +
                  (ux * vy - uy * vx) * (a->z + b->z + c->z);
        ico_f[i] = d > 0.0f ? (p3_face){F[i][0], F[i][1], F[i][2]} : (p3_face){F[i][0], F[i][2], F[i][1]};
    }
    ico = (p3_mesh){ico_v, ico_f, 12, 20};
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
    for (int i = 0; i < MB_MAX; i++)
        n += mb.m[i].on;
    return n;
}

int marbles_hist(int bin) { return mb.hist[bin]; }

void marbles_enter(void)
{
    for (int i = 0; i < MB_MAX; i++)
        mb.m[i].on = 0;
}

void marbles_drop(int note, uint32_t ms, uint32_t land_ms)
{
    (void)ms;
    int i, k = 0;
    for (i = 0; i < MB_MAX; i++) {      /* a free slot, else the oldest marble */
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
}

static float bin_x(int b, int n) { return 8.0f + 224.0f / (float)n * ((float)b + 0.5f); }

void marbles_frame(const uint8_t knobs[3], uint16_t allowed, const char *top_l, const char *top_r, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f;
    int i, n;
    if (!ico.nv)
        ico_build();
    if (!mb.started || allowed != mb.allowed) {   /* other notes allowed: the history starts again */
        __builtin_memset(mb.hist, 0, sizeof mb.hist);
        mb.decay_ms = ms;
        mb.allowed = allowed;
        mb.started = 1;
    }
    while (ms - mb.decay_ms >= MB_DECAY) {
        mb.decay_ms += MB_DECAY;
        for (i = 0; i < 12; i++)
            mb.hist[i] -= mb.hist[i] > 0;
    }
    __builtin_memcpy(mb.knobs, knobs, sizeof mb.knobs);
    for (i = 0; top_l[i] && i < (int)sizeof mb.top_l - 1; i++)
        mb.top_l[i] = top_l[i];
    mb.top_l[i] = 0;
    for (i = 0; top_r[i] && i < (int)sizeof mb.top_r - 1; i++)
        mb.top_r[i] = top_r[i];
    mb.top_r[i] = 0;
    mb.ms = ms;
    p3d_begin();
    for (i = 0; i < MB_MAX; i++) {
        int32_t left = (int32_t)(mb.m[i].land - ms);     /* ms before it lands */
        int b;
        if (!mb.m[i].on)
            continue;
        b = marbles_bin(allowed, mb.m[i].note, &n);
        if (!n) {
            mb.m[i].on = 0;
            continue;
        }
        if (left <= 0) {                /* landed: its note sounds */
            mb.m[i].on = 0;
            mb.hist[b] += mb.hist[b] < 6;
            mb.flash[b] = ms;
            continue;
        }
        if (left >= (int32_t)MB_FALL)   /* not yet falling: it falls for the last MB_FALL ms */
            continue;
        float u = 1.0f - (float)left / (float)MB_FALL, y = 140.0f * u, q = fm_clampf((y - 22.0f) / 19.0f, 0.0f, 5.0f);
        p3_pose pose = {4.0f * t + (float)i, 3.0f * t, 7.0f, 120.0f + (bin_x(b, n) - 120.0f) * q / 5.0f, y};
        p3d_mesh(&ico, &pose, knobs[2] > 64 ? ORANGE : MB_WHITE);
    }
}

void marbles_band(int32_t oy)
{
    float gap = 22.0f + 8.0f * (float)mb.knobs[0] / 127.0f, tilt = ((float)mb.knobs[1] - 64.0f) / 64.0f * 7.0f;
    int i, n, k, deja = mb.knobs[2] > 64;
    cv_rect(0, oy, 240, 166, C_MB_BG);
    cv_text(8, oy + 2, &FONT_S, mb.top_l, C_MB_GRAY);
    cv_text(232 - text_w(&FONT_S, mb.top_r), oy + 2, &FONT_S, mb.top_r, C_MB_GRAY);
    for (k = 0; k < 5; k++)             /* the pins */
        for (i = -5; i <= 5; i++) {
            int32_t x = (int32_t)(120.0f + (float)i * gap + (float)(k % 2) * gap * 0.5f + tilt * (float)k / 4.0f), y = 22 + 19 * k + oy;
            if (x < 10 || x > 230)
                continue;
            cv_rect(x - 3, y, 7, 1, C_PIN);
            cv_rect(x, y - 3, 1, 7, C_PIN);
        }
    marbles_bin(mb.allowed, 0, &n);
    for (i = 0, k = 0; k < 12; k++) {   /* the bins: base, history column, name */
        if (!(mb.allowed >> k & 1u))
            continue;
        float x = bin_x(i, n), h = 4.0f * (float)mb.hist[i], o = (float)oy, f = (float)MB_FLOOR + o;
        int hot = mb.ms - mb.flash[i] < MB_FLASH;
        const uint16_t *T = hot ? PINK3 : deja ? ORANGE : SHIP[0];
        cv_rect((int32_t)(x - 112.0f / (float)n) + 6, MB_FLOOR + oy, (int32_t)(224.0f / (float)n) - 12, 1, C_BASE);
        if (h > 0.0f) {
            p3d_fill(x - 4, f - 1, x + 4, f - 1, x + 4, f - 1 - h, T[1]);
            p3d_fill(x - 4, f - 1, x + 4, f - 1 - h, x - 4, f - 1 - h, T[1]);
            p3d_fill(x - 4, f - 1 - h, x + 4, f - 1 - h, x + 2, f - 3 - h, T[0]);
            p3d_fill(x - 4, f - 1 - h, x + 2, f - 3 - h, x - 6, f - 3 - h, T[0]);
        }
        cv_text((int32_t)x - text_w(&FONT_S, MB_NAME[k]) / 2, MB_FLOOR + 4 + oy, &FONT_S, MB_NAME[k], hot ? PINK3[0] : C_MB_TEXT);
        i++;
    }
    p3d_draw(oy);
}

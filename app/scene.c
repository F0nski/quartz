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
    for (i = 0; i < 30; i++) {                   /* the stars drift left, the big ones faster */
        int big = i % 4 == 0;
        float x = (float)(73 * i) - (big ? 6.0f : 3.0f) * t;
        x -= 240.0f * fm_floorf(x / 240.0f);
        cv_rect((int32_t)x, oy + 4 + (41 * i) % 100, big ? 2 : 1, big ? 2 : 1, big ? C_STAR : C_STAR2);
    }
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

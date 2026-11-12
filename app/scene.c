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

#define MD_N 11                     /* MODAL: quads a side, 242 triangles */
#define MD_V (MD_N + 1)
#define HD_LAT 10                   /* SPEECH: the head's sphere */
#define HD_LON 14
#define HD_NV ((HD_LAT + 1) * HD_LON)
/* the meshes of MODAL, SPEECH and MARBLES: one scene shows at a time, so they share the room (RAM is short), each
   built again when it comes back after another */
static union {
    struct { p3_vec v[MD_V * MD_V]; p3_face f[2 * MD_N * MD_N]; uint8_t part[2 * MD_N * MD_N]; float z[MD_V * MD_V]; } md;
    struct { p3_vec base[HD_NV], v[HD_NV]; p3_face f[2 * HD_LAT * HD_LON]; uint8_t part[2 * HD_LAT * HD_LON]; } hd;
    struct { p3_vec ico_v[12], spire_v[6][10], boulder_v[6][12], crys_v[6]; p3_face ico_f[20], spire_f[13], cf[8]; } fleet;
} sb;
static enum { SB_NONE, SB_MODAL, SB_SPEECH, SB_MARBLES } sb_owner;
static struct { uint32_t hits[3], hit_ms[3]; int seen, model; float t; } sc;

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

#define M_KIT_SCENE 24              /* M_KIT (project.h is not included here) */
static int is_fm(int model) { return model == 2 || model == 3 || model == 4 || model == 10; }
int scene_has(int model) { return is_fm(model) || model == 15 || (model >= 20 && model <= 24); }

/* the notes since the last frame: hit_ms[e] the last one of counter e (the first frame flashes nothing) */
static void take_hits(const uint32_t hits[3], uint32_t ms)
{
    for (int e = 0; e < 3; e++)
        if (!sc.seen || hits[e] != sc.hits[e]) {
            sc.hit_ms[e] = sc.seen ? ms : ms - 100000u;
            sc.hits[e] = hits[e];
        }
    sc.seen = 1;
}

/* a point of the orbit plane, tilted towards the viewer: the far side (depth < 0) is higher on the screen */
static void orbit(float a, float r, float tilt_s, float tilt_c, float *x, float *y, float *z)
{
    float s = fm_sinf(a);
    *x = CX + r * fm_cosf(a);
    *y = CY + r * s * tilt_s;
    *z = r * s * tilt_c;
}

static void planet_frame(const int16_t knobs[4], uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f;    /* wrapped hourly: the angles stay small for fm_sinf */
    float tilt = 0.15f + 1.0f * (float)knobs[2] / 127.0f, ts = fm_sinf(tilt), tc = fm_cosf(tilt);
    float h, gap = 8.0f + 10.0f * (float)knobs[1] / 127.0f;
    int i, n = 2 + (3 * knobs[0] + 63) / 127;
    p3_pose pose = {0.6f * t, 0.0f, R, CX, CY};
    if (!planet.nv)
        p3d_sphere(5, 10, planet_v, planet_f, &planet);
    h = fm_expf(-(float)(ms - sc.hit_ms[0]) / (50.0f + 250.0f * (float)knobs[3] / 127.0f));
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


static void planet_band(int32_t oy)
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


/* ---- KIT and the single drums (spec 5c, 3; docs/screens/scenes.html "Kit") -------------------------------------- */
#define C_BG RGB(0x0c, 0x08, 0x20)
#define C_CYAN RGB(0x5f, 0xe0, 0xff)
#define C_LABEL RGB(0x8b, 0x84, 0xa8)
#define TUN_CY 83                   /* the tunnel's centre in the scene area */
static const uint16_t KBLUE[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
static const uint16_t KGREEN[3] = {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)};
static const uint16_t KIT_EDGE[3] = {RGB(0x0a, 0x14, 0x40), RGB(0x06, 0x30, 0x1f), RGB(0x4a, 0x1d, 0x04)};
static const char *const KIT_NAME[3] = {"BD", "SN", "HH"};
static const float KIT_X[3] = {60.0f, 128.0f, 194.0f}, KIT_Y[3] = {88.0f, 82.0f, 78.0f}, KIT_S[3] = {24.0f, 14.0f, 13.0f};

static const p3_vec OCTA_V[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const p3_vec CUBE_V[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
static const p3_vec TETRA_V[4] = {{1, 1, 1}, {-1, -1, 1}, {-1, 1, -1}, {1, -1, -1}};
static const uint8_t CUBE_Q[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {3, 7, 6, 2}, {0, 1, 5, 4}};
static p3_face octa_f[8], cube_f[12], tetra_f[4], shard_f[12];
static p3_vec shard_v[24];
static p3_mesh octa, cube, tetra, shards;
static struct { int n, kick, edited; float x[3]; } kd;   /* polyhedra shown (1 or 3), the kick's flash, the cyan mark */

/* turn a face of a mesh centred on the origin to face out, its hidden edges following */
static void orient(p3_face *f, const p3_vec *v)
{
    const p3_vec *a = &v[f->a], *b = &v[f->b], *c = &v[f->c];
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
    if ((uy * wz - uz * wy) * (a->x + b->x + c->x) + (uz * wx - ux * wz) * (a->y + b->y + c->y) +
        (ux * wy - uy * wx) * (a->z + b->z + c->z) < 0.0f) {
        uint8_t t = f->b;
        f->b = f->c;
        f->c = t;
        f->hide = (uint8_t)((f->hide & 2u) | (f->hide >> 2 & 1u) | (f->hide << 2 & 4u));   /* a-b and c-a swap */
    }
}

static void kit_meshes(void)
{
    static const uint8_t OF[8][3] = {{0, 4, 2}, {4, 1, 2}, {1, 5, 2}, {5, 0, 2}, {4, 0, 3}, {1, 4, 3}, {5, 1, 3}, {0, 5, 3}};
    static const uint8_t TF[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    int i;
    for (i = 0; i < 8; i++) {
        octa_f[i] = (p3_face){OF[i][0], OF[i][1], OF[i][2], 0};
        orient(&octa_f[i], OCTA_V);
    }
    for (i = 0; i < 4; i++) {
        tetra_f[i] = (p3_face){TF[i][0], TF[i][1], TF[i][2], 0};
        orient(&tetra_f[i], TETRA_V);
    }
    for (i = 0; i < 6; i++) {               /* each square as two triangles, the diagonal hidden */
        const uint8_t *q = CUBE_Q[i];
        cube_f[2 * i] = (p3_face){q[0], q[1], q[2], 4};
        cube_f[2 * i + 1] = (p3_face){q[0], q[2], q[3], 1};
        orient(&cube_f[2 * i], CUBE_V);
        orient(&cube_f[2 * i + 1], CUBE_V);
        shard_f[2 * i] = (p3_face){(uint8_t)(4 * i), (uint8_t)(4 * i + 1), (uint8_t)(4 * i + 2), 4};
        shard_f[2 * i + 1] = (p3_face){(uint8_t)(4 * i), (uint8_t)(4 * i + 2), (uint8_t)(4 * i + 3), 1};
    }
    octa = (p3_mesh){OCTA_V, octa_f, 6, 8};
    cube = (p3_mesh){CUBE_V, cube_f, 8, 12};
    tetra = (p3_mesh){TETRA_V, tetra_f, 4, 4};
    shards = (p3_mesh){shard_v, shard_f, 24, 12};
}

/* the snare's cube with its 6 faces moved out by d along their normals */
static void cube_shards(float d)
{
    for (int i = 0; i < 6; i++) {
        const uint8_t *q = CUBE_Q[i];
        float nx = 0, ny = 0, nz = 0;
        for (int j = 0; j < 4; j++) nx += CUBE_V[q[j]].x, ny += CUBE_V[q[j]].y, nz += CUBE_V[q[j]].z;
        for (int j = 0; j < 4; j++)
            shard_v[4 * i + j] = (p3_vec){CUBE_V[q[j]].x + nx * 0.25f * d, CUBE_V[q[j]].y + ny * 0.25f * d, CUBE_V[q[j]].z + nz * 0.25f * d};
        orient(&shard_f[2 * i], shard_v);       /* the shard's own face: oriented about the cube's centre too */
        orient(&shard_f[2 * i + 1], shard_v);
    }
}

/* k: the KIT's edited pad's TUNE, HARM, TIMB, DECAY, or a single drum's HARM, TIMB, MORPH, DECAY */
static void kit_frame(const q_sview *v, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f;
    int single = v->model != M_KIT_SCENE, e0 = single ? v->model - 21 : 0, e1 = single ? e0 + 1 : 3;
    if (!octa.nv)
        kit_meshes();
    sc.t = t;
    kd.n = e1 - e0;
    kd.kick = 0;
    kd.edited = single ? -1 : v->pad_eng;
    p3d_begin();
    for (int e = e0; e < e1; e++) {
        int mine = single || e == v->pad_eng;
        float tune = mine && !single ? (float)v->k[0] / 24.0f : 0.0f;
        float harm = mine ? (float)v->k[single ? 0 : 1] / 127.0f : 0.5f, timb = mine ? (float)v->k[single ? 1 : 2] / 127.0f : 0.5f;
        float flash = 0.06f + 0.3f * (mine ? (float)v->k[3] : 64.0f) / 127.0f;
        float react = single ? 0.1f + 0.4f * (float)v->k[2] / 127.0f : 0.25f;   /* a single drum's MORPH: its decay */
        float since = (float)(ms - sc.hit_ms[single ? 0 : e]) * 0.001f, amp = 0.3f + 0.7f * harm, spin = 0.3f + 1.2f * timb;
        const uint16_t *tones = since < flash ? PINK3 : e == 0 ? KBLUE : e == 1 ? KGREEN : ORANGE;
        p3_pose p = {spin * t, 0.4f, KIT_S[e] * (1.0f - 0.25f * tune) * (single ? 1.6f : 1.0f), single ? 120.0f : KIT_X[e],
                     single ? 82.0f : KIT_Y[e], 0.0f};
        kd.x[e - e0] = p.cx;
        if (e == 0) {                       /* BD: swells */
            if (since < flash)
                kd.kick = 1;
            if (since < 8.0f * react)
                p.s *= 1.0f + 0.45f * amp * fm_expf(-since / react);
            p3d_mesh_parts(&octa, &p, (const uint16_t (*)[3])tones, 0, &KIT_EDGE[0]);
        } else if (e == 1) {                /* SN: bursts, its faces out and back */
            p.ax = 0.5f;
            if (since < react) {
                cube_shards(1.6f * amp * fm_sinf(FM_PI * since / react));
                p.ay += 0.5f * FM_PI * since / react;   /* a quarter turn: the cube closes as it was */
                p3d_mesh_parts(&shards, &p, (const uint16_t (*)[3])tones, 0, &KIT_EDGE[1]);
            } else {
                p3d_mesh_parts(&cube, &p, (const uint16_t (*)[3])tones, 0, &KIT_EDGE[1]);
            }
        } else {                            /* HH: a quarter turn a hit with a hop as high as HARM, done in 120 ms (a
                                               single hi-hat: as long as its MORPH says) */
            uint32_t n = sc.hits[single ? 0 : 2];
            float q = (float)(n & 3u), dur = single ? 0.4f * react : 0.12f;
            if (n && since < dur) {
                q += since / dur - 1.0f;
                p.cy -= 8.0f * amp * fm_sinf(FM_PI * since / dur);
            }
            p.ax = 0.6f + q * (0.5f * FM_PI);
            p3d_mesh_parts(&tetra, &p, (const uint16_t (*)[3])tones, 0, &KIT_EDGE[2]);
        }
    }
}

static void kit_band(int32_t oy)
{
    float f = 1.2f * sc.t - fm_floorf(1.2f * sc.t);
    int i;
    cv_rect(0, oy, 240, 166, C_BG);
    for (i = 0; i < 2; i++) {               /* the vanishing lines to the corners */
        cv_line(120, oy + TUN_CY, 240 * i, oy, RGB(0x22, 0x18, 0x4a));
        cv_line(120, oy + TUN_CY, 240 * i, oy + 165, RGB(0x22, 0x18, 0x4a));
    }
    for (i = 0; i < 7; i++) {               /* the tunnel's squares rushing out, the nearest pink on a kick */
        float p = ((float)i + f) / 7.0f, w;
        int32_t x0, y0, ww, hh, th;
        uint16_t c;
        p *= p;
        w = 10.0f + p * 230.0f;
        x0 = (int32_t)(120.0f - w * 0.5f), y0 = (int32_t)((float)TUN_CY - w * 0.36f), ww = (int32_t)w, hh = (int32_t)(w * 0.72f);
        c = kd.kick && i == 6 ? PINK3[1] : p > 0.5f ? RGB(0x3a, 0x24, 0x70) : RGB(0x22, 0x18, 0x4a);
        th = kd.kick && p > 0.6f ? 2 : 1;
        cv_rect(x0, oy + y0, ww, th, c);
        cv_rect(x0, oy + y0 + hh - th, ww, th, c);
        cv_rect(x0, oy + y0, th, hh, c);
        cv_rect(x0 + ww - th, oy + y0, th, hh, c);
    }
    p3d_draw(oy);
    if (kd.n == 3)
        for (i = 0; i < 3; i++) {
            int32_t x = (int32_t)kd.x[i];
            cv_text(x - text_w(&FONT_S, KIT_NAME[i]) / 2, oy + 140, &FONT_S, KIT_NAME[i], C_LABEL);
            if (i == kd.edited)
                cv_rect(x - 10, oy + 157, 20, 2, C_CYAN);
        }
}

/* ---- MODAL: a Chladni relief (spec 5c, 4; docs/screens/scenes.html "Modal") ------------------------------------ */
static const uint16_t MD_TONES[3][3] = {
    {RGB(0x8f, 0xf5, 0xc8), RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e)},     /* the nodal lines */
    {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)},     /* above */
    {RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48), RGB(0x0a, 0x3f, 0x2a)}};    /* below */
static const uint16_t MD_EDGE = RGB(0x0a, 0x0f, 0x2e);
static p3_mesh plate;
static int pl_m, pl_n;

static float chladni(float x, float y, int m, int n)
{
    float X = x * FM_PI, Y = y * FM_PI;
    return fm_cosf((float)n * X) * fm_cosf((float)m * Y) - fm_cosf((float)m * X) * fm_cosf((float)n * Y);
}

/* the mode (m, n): the shape at the vertices, and each face's part from the shape at its centre */
static void plate_mode(int m, int n)
{
    int i, j;
    pl_m = m, pl_n = n;
    sb_owner = SB_MODAL;
    for (j = 0; j < MD_V; j++)
        for (i = 0; i < MD_V; i++) {
            sb.md.z[j * MD_V + i] = chladni((float)i / MD_N, (float)j / MD_N, m, n);
            sb.md.v[j * MD_V + i].x = ((float)i / MD_N - 0.5f) * 2.0f;
            sb.md.v[j * MD_V + i].z = ((float)j / MD_N - 0.5f) * 2.0f;
        }
    for (j = 0; j < MD_N; j++)
        for (i = 0; i < MD_N; i++) {
            uint8_t a = (uint8_t)(j * MD_V + i), b = (uint8_t)(a + MD_V), c = (uint8_t)(a + MD_V + 1), d = (uint8_t)(a + 1);
            int k = 2 * (j * MD_N + i);
            float z = chladni(((float)i + 0.5f) / MD_N, ((float)j + 0.5f) / MD_N, m, n);
            sb.md.f[k] = (p3_face){a, b, c, 4};
            sb.md.f[k + 1] = (p3_face){a, c, d, 1};
            sb.md.part[k] = sb.md.part[k + 1] = (uint8_t)(fm_fabsf(z) < 0.12f ? 0 : z > 0.0f ? 1 : 2);
        }
    plate = (p3_mesh){sb.md.v, sb.md.f, MD_V * MD_V, 2 * MD_N * MD_N};
}

/* k: HARM (the mode), TIMB (the strike's force), MORPH (its damping), DECAY (the pink) */
static void modal_frame(const q_sview *v, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f, since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    float force = 0.4f + 0.6f * (float)v->k[1] / 127.0f, tau = 0.15f + 0.6f * (float)v->k[2] / 127.0f;
    float amp = 0.16f, sz, cy;
    int f = v->k[0] * 9 / 128, m = 1 + f / 3, n = m + 1 + f % 3, i;   /* 9 figures: m 1..3, n m + 1..m + 3 */
    uint16_t tones[3][3];
    p3_pose p = {0.6f + 0.25f * fm_sinf(0.2f * t), 0.95f, 62.0f, 120.0f, 82.0f, 0.0f};
    if (sb_owner != SB_MODAL || m != pl_m || n != pl_n)
        plate_mode(m, n);
    if (since < 6.0f * tau)                    /* a strike: the plate shivers, then settles */
        amp += 0.05f * fm_expf(-since / tau) * force * fm_sinf(16.0f * since);
    for (i = 0; i < MD_V * MD_V; i++)
        sb.md.v[i].y = -sb.md.z[i] * amp;
    __builtin_memcpy(tones, MD_TONES, sizeof tones);
    if (since < 0.15f + 0.6f * (float)v->k[3] / 127.0f)
        __builtin_memcpy(tones[0], PINK3, sizeof tones[0]);
    sc.t = t;
    p3d_begin();
    p3d_mesh_parts(&plate, &p, (const uint16_t (*)[3])tones, sb.md.part, &MD_EDGE);
    sz = 6.0f + 4.0f * (float)v->k[1] / 127.0f;        /* the striker, dropping on a note */
    cy = 10.0f + (since < 0.12f ? 14.0f * (1.0f - since / 0.12f) : 0.0f);
    p3d_tri(120.0f, cy - sz, 120.0f + sz, cy, 120.0f - sz, cy, 1000.0f, ORANGE[0]);
    p3d_tri(120.0f + sz, cy, 120.0f, cy + sz, 120.0f - sz, cy, 1000.0f, ORANGE[1]);
}

static void modal_band(int32_t oy)
{
    cv_rect(0, oy, 240, 166, C_BG);
    for (int r = 1; r <= 5; r++)                /* rings on the floor */
        for (int k = 0; k < 24; k++) {
            float a0 = (float)k * (FM_TWO_PI / 24.0f), a1 = a0 + FM_TWO_PI / 24.0f;
            cv_line((int32_t)(120.0f + 24.0f * (float)r * fm_cosf(a0)), oy + (int32_t)(94.0f + 9.0f * (float)r * fm_sinf(a0)),
                    (int32_t)(120.0f + 24.0f * (float)r * fm_cosf(a1)), oy + (int32_t)(94.0f + 9.0f * (float)r * fm_sinf(a1)),
                    RGB(0x1a, 0x14, 0x38));
        }
    p3d_draw(oy);
}

/* ---- SPEECH: the head that speaks (spec 5c, 5; docs/screens/speech.html) ------------------------------------------ */
#define C_PUPIL RGB(0x8f, 0xf5, 0xff)
static const uint16_t SP_VIOLET[3] = {RGB(0xe4, 0xdd, 0xff), RGB(0x9a, 0x7c, 0xff), RGB(0x4a, 0x36, 0xb0)};
static const uint16_t SP_EYE[3] = {RGB(0x0b, 0x2a, 0x1f), RGB(0x06, 0x20, 0x1a), RGB(0x03, 0x14, 0x0f)};
static const uint16_t SP_MOUTH[3] = {RGB(0xff, 0x2f, 0x8e), RGB(0x8a, 0x10, 0x50), RGB(0x4a, 0x08, 0x28)};
static const uint16_t SP_WIRE[3] = {RGB(0x43, 0xdb, 0x9f), RGB(0x57, 0x83, 0xff), RGB(0xa9, 0x90, 0xff)};   /* the tints' base, 15 % white */
static p3_mesh hd_mesh;
static struct { float open; int tint; } spk;

/* a head from a sphere: skull, brow, eye sockets, nose, cheeks, a narrowing jaw and chin; model y up */
static void head_mesh(void)
{
    p3_mesh m;
    sb_owner = SB_SPEECH;
    p3d_sphere(HD_LAT, HD_LON, sb.hd.base, sb.hd.f, &m);
    for (int i = 0; i < HD_NV; i++) {
        p3_vec *q = &sb.hd.base[i];
        float x = q->x * 0.8f, y = q->y * 1.12f, z = q->z, ax;
        if (y < 0.0f) x *= 1.0f + y * 0.38f;                                         /* the jaw narrows */
        ax = fm_fabsf(x);
        if (y < -0.75f && z > 0.0f) z *= 1.08f;                                       /* chin */
        if (z > 0.55f && y > 0.18f && y < 0.36f) z += 0.09f;                          /* brow ridge */
        if (z > 0.45f && y > -0.05f && y < 0.2f && ax > 0.14f && ax < 0.5f) z -= 0.14f;   /* eye sockets */
        if (z > 0.7f && ax < 0.16f && y > -0.3f && y < 0.15f)                         /* nose */
            z += 0.32f * (1.0f - ax / 0.16f) * (1.0f - fm_fabsf(y + 0.08f) / 0.25f);
        if (z > 0.3f && ax > 0.42f && y > -0.35f && y < 0.0f) { x *= 1.06f; z += 0.05f; }   /* cheekbones */
        *q = (p3_vec){x, y, z};
    }
    for (int i = 0; i < m.nf; i++) {           /* a quad's part from its whole: its two halves are faces i, i + 1 */
        int k = sb.hd.f[i].hide == 1 ? i - 1 : i;  /* the quad's first half, or a pole's triangle */
        const p3_vec *a = &sb.hd.base[sb.hd.f[k].a], *b = &sb.hd.base[sb.hd.f[k].b], *c = &sb.hd.base[sb.hd.f[k].c];
        const p3_vec *d = sb.hd.f[k].hide == 4 ? &sb.hd.base[sb.hd.f[k + 1].c] : c;
        float x = (a->x + b->x + c->x + d->x) / 4.0f, y = (a->y + b->y + c->y + d->y) / 4.0f, z = (a->z + b->z + c->z + d->z) / 4.0f;
        float ax = fm_fabsf(x);
        sb.hd.part[i] = (uint8_t)(z > 0.35f && y > -0.08f && y < 0.2f && ax > 0.12f && ax < 0.5f ? 1 :
                               z > 0.4f && y > -0.62f && y < -0.42f && ax < 0.32f ? 2 : 0);
    }
    for (int i = 0; i < m.nf; i++) {
        p3_face *f = &sb.hd.f[i];
        uint8_t t = f->b;                       /* drawn with y down: the mirror turns the faces, turn them back */
        f->b = f->c;
        f->c = t;
        f->hide = (uint8_t)((f->hide & 2u) | (f->hide >> 2 & 1u) | (f->hide << 2 & 4u));
    }
    hd_mesh = (p3_mesh){sb.hd.v, sb.hd.f, HD_NV, m.nf};
}

/* k: HARM (the voice model: the tint), TIMB (the species: the size), MORPH (the phoneme: the mouth), DECAY (syllables) */
static void speech_frame(const q_sview *v, uint32_t ms)
{
    static const uint16_t *const TINT[3] = {KGREEN, KBLUE, SP_VIOLET};
    float t = (float)(ms % 3600000u) * 0.001f, since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    float dur = 0.15f + 0.35f * (float)v->k[3] / 127.0f, size = 0.85f + 0.3f * (float)v->k[1] / 127.0f, open = 0.0f;
    float ay = 0.6f * fm_sinf(0.5f * t), ax = -0.08f, sy = fm_sinf(ay), cy = fm_cosf(ay), sx = fm_sinf(ax), cx = fm_cosf(ax);
    int tint = v->k[0] * 3 / 128;
    uint16_t tones[3][3], edge;
    p3_pose p = {ay, ax, 48.0f, 120.0f, 74.0f, 0.0f};
    if (sb_owner != SB_SPEECH)
        head_mesh();
    if (since < dur)                           /* two syllables a note */
        open = fm_fabsf(fm_sinf(FM_TWO_PI * since / dur)) * (0.4f + 0.6f * (float)v->k[2] / 127.0f);
    for (int i = 0; i < HD_NV; i++) {
        const p3_vec *b = &sb.hd.base[i];
        float y = b->y < -0.5f && b->z > -0.3f ? b->y - open * 0.3f : b->y;     /* the jaw drops */
        sb.hd.v[i] = (p3_vec){b->x * size, -y * size, b->z * size};
    }
    __builtin_memcpy(tones[0], TINT[tint], sizeof tones[0]);
    __builtin_memcpy(tones[1], SP_EYE, sizeof tones[1]);
    __builtin_memcpy(tones[2], open > 0.15f ? SP_MOUTH : SP_EYE, sizeof tones[2]);
    edge = open > 0.3f ? PINK3[1] : SP_WIRE[tint];
    sc.t = t;
    spk.open = open;
    spk.tint = tint;
    p3d_begin();
    p3d_mesh_parts(&hd_mesh, &p, (const uint16_t (*)[3])tones, sb.hd.part, &edge);
    for (int s = -1; s <= 1; s += 2) {          /* two small neon pupils on the dark eye band, seen from the front */
        float x = 0.24f * size * (float)s, y = -0.1f * size, z = 0.66f * size;
        float rx = x * cy + z * sy, rz = -x * sy + z * cy, ry = y * cx - rz * sx, d = y * sx + rz * cx, px, py;
        uint16_t c = open > 0.4f ? PINK3[0] : C_PUPIL;
        if (d < 0.25f * size)
            continue;
        px = 120.0f + rx * 48.0f, py = 74.0f + ry * 48.0f;
        p3d_tri(px - 3.0f, py, px, py - 2.0f, px + 3.0f, py, 1000.0f, c);   /* on top, as the mock-up draws them */
        p3d_tri(px - 3.0f, py, px + 3.0f, py, px, py + 2.0f, 1000.0f, c);
    }
}

static void speech_band(int32_t oy)
{
    static const uint16_t *const TINT[3] = {KGREEN, KBLUE, SP_VIOLET};
    float t = sc.t, scan = 60.0f * t;
    int i;
    cv_rect(0, oy, 240, 166, C_BG);
    for (i = 4; i < 240; i += 12)               /* the scan grid */
        cv_rect(i, oy, 1, 166, RGB(0x1e, 0x16, 0x40));
    for (i = 6; i < 166; i += 12)
        cv_rect(0, oy + i, 240, 1, RGB(0x1e, 0x16, 0x40));
    cv_rect(0, oy + (int32_t)(scan - 166.0f * fm_floorf(scan / 166.0f)), 240, 1, KGREEN[2]);
    p3d_draw(oy);
    for (i = 0; i < 9; i++) {                   /* the phoneme bars */
        int32_t h = (int32_t)(fm_fabsf(fm_sinf(1.7f * (float)i + 2.3f * fm_floorf(4.0f * t))) * spk.open * 24.0f);
        if (h > 0)
            cv_rect(30 + 20 * i, oy + 158 - h, 14, h, TINT[spk.tint][i % 2 ? 1 : 2]);
    }
}

void scene_frame(const q_sview *v, uint32_t ms)
{
    take_hits(v->hits, ms);
    sc.model = v->model;
    if (is_fm(v->model))
        planet_frame(v->k, ms);
    else if (v->model >= 21 && v->model <= M_KIT_SCENE)
        kit_frame(v, ms);
    else if (v->model == 20)
        modal_frame(v, ms);
    else if (v->model == 15)
        speech_frame(v, ms);
    else
        p3d_begin();
}

void scene_band(int32_t oy)
{
    if (is_fm(sc.model))
        planet_band(oy);
    else if (sc.model >= 21 && sc.model <= M_KIT_SCENE)
        kit_band(oy);
    else if (sc.model == 20)
        modal_band(oy);
    else if (sc.model == 15)
        speech_band(oy);
    else
        cv_rect(0, oy, 240, 166, C_SKY);
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

static p3_mesh ico, boulder[6], spire[3];
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
    sb_owner = SB_MARBLES;
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
        sb.fleet.ico_v[i] = (p3_vec){c[0] * n, c[1] * n, c[2] * n};
    }
    for (i = 0; i < 20; i++) {
        sb.fleet.ico_f[i] = (p3_face){F[i][0], F[i][1], F[i][2], 0};
        face_out(&sb.fleet.ico_f[i], sb.fleet.ico_v);  /* (centred: the 1.5 shifts the centre, harmless for a sphere around 0) */
    }
    ico = (p3_mesh){sb.fleet.ico_v, sb.fleet.ico_f, 12, 20};
    for (k = 0; k < 6; k++) {       /* boulders: a bumpy icosahedron, its bottom flattened to stand on y = 0 */
        for (i = 0; i < 12; i++) {
            p3_vec q = sb.fleet.ico_v[i];
            float j = 0.35f, y = q.y - 0.55f;
            sb.fleet.boulder_v[k][i] = (p3_vec){1.1f * q.x + (p3d_hash((uint32_t)(k * 97 + i * 7)) - 0.5f) * j,
                                       1.1f * (y < 0.0f ? y : 0.0f) + (p3d_hash((uint32_t)(k * 97 + i * 7 + 1)) - 0.5f) * j,
                                       q.z + (p3d_hash((uint32_t)(k * 97 + i * 7 + 2)) - 0.5f) * j};
        }
        boulder[k] = (p3_mesh){sb.fleet.boulder_v[k], sb.fleet.ico_f, 12, 20};
    }
    for (k = 0; k < 3; k++) {       /* spires: a 5-sided column narrowing to 0.18 at 3.2 high */
        for (i = 0; i < 5; i++) {
            float a = (float)i * (FM_TWO_PI / 5.0f) + p3d_hash((uint32_t)(k * 31 + i)) * 0.5f, r = 0.85f + p3d_hash((uint32_t)(k * 31 + i + 9)) * 0.3f;
            sb.fleet.spire_v[k][2 * i] = (p3_vec){fm_cosf(a) * r, 0.0f, fm_sinf(a) * r};
            sb.fleet.spire_v[k][2 * i + 1] = (p3_vec){fm_cosf(a) * r * 0.18f, -3.2f, fm_sinf(a) * r * 0.18f};
        }
        spire[k] = (p3_mesh){sb.fleet.spire_v[k], sb.fleet.spire_f, 10, 13};
    }
    for (i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        sb.fleet.spire_f[2 * i] = (p3_face){(uint8_t)(2 * i), (uint8_t)(2 * j), (uint8_t)(2 * j + 1), 0};
        sb.fleet.spire_f[2 * i + 1] = (p3_face){(uint8_t)(2 * i), (uint8_t)(2 * j + 1), (uint8_t)(2 * i + 1), 0};
    }
    sb.fleet.spire_f[10] = (p3_face){1, 3, 5, 0};
    sb.fleet.spire_f[11] = (p3_face){1, 5, 7, 0};
    sb.fleet.spire_f[12] = (p3_face){1, 7, 9, 0};
    for (i = 0; i < 13; i++)
        face_out(&sb.fleet.spire_f[i], sb.fleet.spire_v[0]);
    static const p3_face CF[8] = {{0, 4, 2, 0}, {4, 1, 2, 0}, {1, 5, 2, 0}, {5, 0, 2, 0}, {4, 0, 3, 0}, {1, 4, 3, 0}, {5, 1, 3, 0}, {0, 5, 3, 0}};
    sb.fleet.crys_v[0] = (p3_vec){0.7f, 0, 0}, sb.fleet.crys_v[1] = (p3_vec){-0.7f, 0, 0}, sb.fleet.crys_v[2] = (p3_vec){0, 1.4f, 0};
    sb.fleet.crys_v[3] = (p3_vec){0, -1.4f, 0}, sb.fleet.crys_v[4] = (p3_vec){0, 0, 0.7f}, sb.fleet.crys_v[5] = (p3_vec){0, 0, -0.7f};
    for (i = 0; i < 8; i++) {
        sb.fleet.cf[i] = CF[i];
        const p3_vec *a = &sb.fleet.crys_v[sb.fleet.cf[i].a], *b = &sb.fleet.crys_v[sb.fleet.cf[i].b], *c = &sb.fleet.crys_v[sb.fleet.cf[i].c];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
        if ((uy * wz - uz * wy) * (a->x + b->x + c->x) + (uz * wx - ux * wz) * (a->y + b->y + c->y) + (ux * wy - uy * wx) * (a->z + b->z + c->z) < 0.0f) {
            uint8_t t = sb.fleet.cf[i].b;
            sb.fleet.cf[i].b = sb.fleet.cf[i].c;
            sb.fleet.cf[i].c = t;
        }
    }
    crystal = (p3_mesh){sb.fleet.crys_v, sb.fleet.cf, 6, 8};
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
    if (sb_owner != SB_MARBLES)
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

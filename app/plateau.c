/* The SEQ plateau (plateau.h, docs/screens/sequencer.html "Plateau 4x4" and "Pas tenu"). Part of the unity build
 * (app.c), after p3d.c. Drawn straight into each band, far to near (gx + gy), so that a block hides the name on the
 * block behind it. */
#include "plateau.h"
#include "fastmath.h"

#define PL_RGB(r, g, b) RGB(r, g, b)
static const uint16_t PL_WHITE[3] = {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)};
static const uint16_t PL_DIM[3] = {RGB(0x4a, 0x44, 0x66), RGB(0x33, 0x2e, 0x4c), RGB(0x22, 0x1d, 0x36)};
static const uint16_t PL_ORANGE[3] = {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)};
static const uint16_t PL_GREEN[3] = {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)};
static const uint16_t PL_PINK[3] = {RGB(0xff, 0xb8, 0xdc), RGB(0xff, 0x2f, 0x8e), RGB(0x8a, 0x10, 0x50)};
#define PL_BG RGB(0x07, 0x04, 0x1a)
#define PL_RAY RGB(0x16, 0x0f, 0x36)
#define PL_CUBE RGB(0x2a, 0x1d, 0x55)
#define PL_CELL RGB(0x0e, 0x0a, 0x26)
#define PL_EDGE RGB(0x2a, 0x1d, 0x55)
#define PL_OUT RGB(0x06, 0x03, 0x1a)
#define PL_OUT_EDGE RGB(0x12, 0x0c, 0x2c)
#define PL_OFF RGB(0x16, 0x11, 0x2f)
#define PL_OFF_CUR RGB(0x2f, 0x27, 0x56)
#define PL_PULSE RGB(0x5b, 0x2f, 0x8c)
#define PL_INK RGB(0x06, 0x04, 0x1a)
#define PL_INK_HOT RGB(0x3a, 0x06, 0x20)
#define PL_PATH RGB(0x8a, 0x6a, 0xd8)
#define PL_ARROW RGB(0xb8, 0x9c, 0xff)
#define PL_STRIP_DIM RGB(0x1f, 0x3a, 0x33)
#define PL_MARK_DIM RGB(0x4a, 0x3a, 0x22)
#define PL_RULE RGB(0x1a, 0x14, 0x33)
#define PL_LABEL RGB(0x8b, 0x84, 0xa8)
#define PL_TEXT RGB(0xf4, 0xf1, 0xff)
#define HIT_MS 150u
#define PULSE_MS 200u

static struct {
    q_plateau v;
    uint32_t ms, hits, hit_ms, pulse_ms;
    int seen, hit_cell, last_pos;
} pl;

typedef struct { float x, y; } pl_pt;
static float A, B, OX, OY;           /* the layout of this frame: half a cell on screen, the top corner */

static pl_pt at(float gx, float gy) { return (pl_pt){OX + (gx - gy) * A, OY + (gx + gy) * B}; }
static pl_pt up(pl_pt p, float h) { return (pl_pt){p.x, p.y - h}; }

static void quad(pl_pt a, pl_pt b, pl_pt c, pl_pt d, int32_t oy, uint16_t col)
{
    float o = (float)oy;
    p3d_fill(a.x, a.y + o, b.x, b.y + o, c.x, c.y + o, col);
    p3d_fill(a.x, a.y + o, c.x, c.y + o, d.x, d.y + o, col);
}

static void line(pl_pt a, pl_pt b, int32_t oy, uint16_t col)
{
    cv_line((int32_t)(a.x + 0.5f), (int32_t)(a.y + 0.5f) + oy, (int32_t)(b.x + 0.5f), (int32_t)(b.y + 0.5f) + oy, col);
}

static void outline(pl_pt a, pl_pt b, pl_pt c, pl_pt d, int32_t oy, uint16_t col)
{
    line(a, b, oy, col);
    line(b, c, oy, col);
    line(c, d, oy, col);
    line(d, a, oy, col);
}

static void text_c(float x, float y, const felucca_font_t *f, const char *s, uint16_t c, int32_t oy)
{
    cv_text((int32_t)x - text_w(f, s) / 2, (int32_t)y - f->h / 2 + oy, f, s, c);
}

void plateau_enter(void) { pl.seen = 0; }

void plateau_frame(const q_plateau *p, uint32_t hits, uint32_t ms)
{
    if (!pl.seen) {                     /* the notes before the plateau showed do not hit */
        pl.hits = hits;
        pl.hit_ms = pl.pulse_ms = ms - 100000u;
        pl.last_pos = -1;
        pl.seen = 1;
    } else if (hits != pl.hits) {
        pl.hits = hits;
        if (p->pos >= 0) {
            pl.hit_ms = ms;
            pl.hit_cell = p->pos;
        }
    }
    if (p->pos != pl.last_pos && p->pos >= 0 && p->pos % 4 == 0)   /* a beat at 1/16: the floor pulses */
        pl.pulse_ms = ms;
    pl.last_pos = p->pos;
    pl.v = *p;
    pl.ms = ms;
}

/* the city (spec 5b, 2): two rows of wire towers on the horizon drifting left, a floor of vanishing lines */
#define PL_HZ 48
#define PL_TOWER1 RGB(0x2e, 0x20, 0x60)
#define PL_TOWER2 RGB(0x4a, 0x34, 0x88)
#define PL_GROUND RGB(0x0a, 0x06, 0x20)
#define PL_LINE RGB(0x1c, 0x13, 0x40)
static void backdrop(int32_t oy)
{
    float t = (float)(pl.ms % 3600000u) * 0.001f, f = 0.5f * t - fm_floorf(0.5f * t), d = t * 6.0f;
    int k, i;
    cv_rect(0, oy, 240, 166, PL_BG);
    stars(oy, t, 24, 2, 40, 0.05f);
    for (k = 0; k < 2; k++) {           /* the far row, then the near one: faster and brighter */
        float sp = k ? 1.0f : 0.5f, sh = d * sp - 30.0f * fm_floorf(d * sp / 30.0f);
        uint32_t base = (uint32_t)(int32_t)fm_floorf(d * sp / 30.0f);
        for (i = -1; i < 10; i++) {
            uint32_t id = base + (uint32_t)i + (uint32_t)k * 1000u;
            int32_t x = (int32_t)((float)(i * 30) - sh + p3d_hash(id) * 10.0f), w = 8 + (int32_t)(p3d_hash(id * 3u) * 12.0f);
            int32_t h = k ? 14 + (int32_t)(p3d_hash(id * 7u) * 22.0f) : 22 + (int32_t)(p3d_hash(id * 7u) * 30.0f), y0 = oy + PL_HZ;
            uint16_t edge = k ? PL_TOWER2 : PL_TOWER1;
            cv_rect(x, y0 - h, w, h, k ? RGB(0x1a, 0x12, 0x40) : RGB(0x12, 0x0c, 0x2e));
            cv_rect(x, y0 - h, 1, h, edge);
            cv_rect(x + w - 1, y0 - h, 1, h, edge);
            cv_rect(x, y0 - h, w, 1, edge);
            if (p3d_hash(id * 11u) > 0.6f)                       /* an antenna */
                cv_rect(x + w / 2, y0 - h - 8, 1, 8, edge);
            for (int fl = 1; fl < h / 6; fl++)                   /* a few lit windows */
                if (p3d_hash(id * 13u + (uint32_t)fl) > 0.55f)
                    cv_rect(x + 2 + (fl % 2) * (w - 6), y0 - fl * 6, 2, 2, k ? RGB(0x8a, 0x6a, 0xd8) : PL_PULSE);
        }
    }
    cv_rect(0, oy + PL_HZ + 1, 240, 166 - PL_HZ - 1, PL_GROUND);
    cv_rect(0, oy + PL_HZ, 240, 1, PL_PULSE);
    for (k = -12; k <= 12; k++)
        cv_line(120 + 10 * k, oy + PL_HZ, 120 + 60 * k, oy + 166, PL_LINE);
    for (k = 0; k < 6; k++) {
        float p = ((float)k + f) / 6.0f;
        cv_rect(0, oy + PL_HZ + (int32_t)((166.0f - PL_HZ) * p * p), 240, 1, PL_LINE);
    }
}

static void block(int i, int32_t oy, int held, int hot, float k)
{
    const q_pcell *c = &pl.v.c[i];
    float gx = (float)(i % 4), gy = (float)(i / 4), m = 0.18f;
    int is_held = i == pl.v.held;
    const uint16_t *T = is_held ? PL_ORANGE : held ? PL_DIM : hot ? PL_PINK : PL_WHITE;
    float lift = is_held ? 10.0f : hot ? 6.0f * (1.0f - k) : 0.0f;
    float h = 5.0f + (float)c->vel * (held ? 16.0f : 22.0f) / 127.0f;
    pl_pt p0 = at(gx + m, gy + m), p1 = at(gx + 1 - m, gy + m), p2 = at(gx + 1 - m, gy + 1 - m), p3 = at(gx + m, gy + 1 - m);
    pl_pt top;
    if (!c->on) {
        quad(p0, p1, p2, p3, oy, i == pl.v.pos ? PL_OFF_CUR : PL_OFF);
        return;
    }
    if (c->len > 4) {                   /* a long note: a green strip on the floor towards the next cells */
        float L = fm_minf((float)c->len / 8.0f, 4.0f - gx - m);
        quad(at(gx + 0.5f, gy + 0.62f), at(gx + 0.5f + L, gy + 0.62f), at(gx + 0.5f + L, gy + 0.72f), at(gx + 0.5f, gy + 0.72f), oy,
             held && !is_held ? PL_STRIP_DIM : PL_GREEN[2]);
    }
    quad(up(p3, lift), up(p2, lift), up(p2, h + lift), up(p3, h + lift), oy, T[1]);
    quad(up(p1, lift), up(p2, lift), up(p2, h + lift), up(p1, h + lift), oy, T[2]);
    quad(up(p0, h + lift), up(p1, h + lift), up(p2, h + lift), up(p3, h + lift), oy, T[0]);
    outline(up(p0, h + lift), up(p1, h + lift), up(p2, h + lift), up(p3, h + lift), oy, T[2]);
    top = up(at(gx + 0.5f, gy + 0.5f), h + lift);
    text_c(top.x, top.y, &FONT_T, c->name, hot ? PL_INK_HOT : PL_INK, oy);
    if (c->extra) {                     /* the step has more than a note */
        pl_pt q = up(p1, h + lift);
        float o = (float)oy;
        p3d_fill(q.x - 6, q.y - 1 + o, q.x, q.y - 4 + o, q.x, q.y + 2 + o, held && !is_held ? PL_MARK_DIM : PL_ORANGE[1]);
    }
    if (hot)                            /* shards flying up */
        for (int s = 0; s < 3; s++) {
            float sx = top.x + (float)(s - 1) * 9.0f, sy = top.y - 6.0f - k * 26.0f - (float)s * 3.0f + (float)oy;
            p3d_fill(sx, sy - 3, sx + 3, sy + 1, sx - 2, sy + 2, s % 2 ? PL_PINK[0] : PL_PINK[1]);
        }
}

static void sight(int cell, int cols, uint16_t col, float g, int32_t oy)   /* cols: the plate's width in cells */
{
    float gx = (float)(cell % cols), gy = (float)(cell / cols);
    pl_pt cs[4] = {at(gx - g, gy - g), at(gx + 1 + g, gy - g), at(gx + 1 + g, gy + 1 + g), at(gx - g, gy + 1 + g)};
    for (int k = 0; k < 4; k++) {       /* brackets: each corner and 30 % of its two edges, 2 px */
        pl_pt p = cs[k], a = cs[(k + 3) % 4], b = cs[(k + 1) % 4];
        pl_pt pa = {p.x + (a.x - p.x) * 0.3f, p.y + (a.y - p.y) * 0.3f}, pb = {p.x + (b.x - p.x) * 0.3f, p.y + (b.y - p.y) * 0.3f};
        for (int d = 0; d < 2; d++) {
            pl_pt q = {p.x + (float)d, p.y}, qa = {pa.x + (float)d, pa.y}, qb = {pb.x + (float)d, pb.y};
            line(qa, q, oy, col);
            line(q, qb, oy, col);
        }
    }
}

static void path(int32_t oy)
{
    for (int k = 0; k + 1 < pl.v.npath; k++) {
        int s = pl.v.path[k], e = pl.v.path[k + 1];
        pl_pt a = at((float)(s % 4) + 0.5f, (float)(s / 4) + 0.5f), b = at((float)(e % 4) + 0.5f, (float)(e / 4) + 0.5f);
        float dx = b.x - a.x, dy = b.y - a.y, l = fm_sqrtf(dx * dx + dy * dy), mx = 0.5f * (a.x + b.x), my = 0.5f * (a.y + b.y) + (float)oy;
        line(a, b, oy, PL_PATH);
        if (l <= 0.0f)
            continue;
        dx /= l;
        dy /= l;
        p3d_fill(mx + dx * 4, my + dy * 4, mx - dx * 3 - dy * 3, my - dy * 3 + dx * 3, mx - dx * 3 + dy * 3, my - dy * 3 - dx * 3, PL_ARROW);
    }
}

static void held_panel(int32_t oy)
{
    const int32_t x0 = 128, w = 106, rh = 17;
    for (int r = 0; r < 9; r++) {
        int32_t y = 8 + r * rh + oy;
        int sel = r == pl.v.attr;
        if (sel) {
            float yf = (float)y;
            p3d_fill((float)x0, yf, (float)(x0 + w), yf, (float)(x0 + w), yf + 15, PL_ORANGE[1]);
            p3d_fill((float)x0, yf, (float)(x0 + w), yf + 15, (float)(x0 + 6), yf + 15, PL_ORANGE[1]);
            p3d_fill((float)x0, yf, (float)(x0 + 6), yf + 15, (float)x0, yf + 9, PL_ORANGE[1]);
        } else {
            cv_rect(x0, y + rh - 2, w, 1, PL_RULE);
        }
        cv_text(x0 + 8, y + 1, &FONT_XS, pl.v.rows[r][0], sel ? PL_INK : PL_LABEL);
        cv_text(x0 + w - 6 - text_w(&FONT_S, pl.v.rows[r][1]), y, &FONT_S, pl.v.rows[r][1], sel ? PL_INK : PL_TEXT);
    }
    text_c(64, 150, &FONT_M, pl.v.held_name, PL_ORANGE[0], oy);
}

void plateau_band(int32_t oy)
{
    int held = pl.v.held >= 0, i, s;
    uint32_t dt = pl.ms - pl.hit_ms;
    int hit = !held && dt < HIT_MS && pl.v.pos == pl.hit_cell;
    float k = hit ? (float)dt / (float)HIT_MS : 0.0f;
    int pulse = !held && pl.v.pos >= 0 && pl.ms - pl.pulse_ms < PULSE_MS;
    if (held)
        A = 16.0f, B = 8.0f, OX = 64.0f, OY = 48.0f;
    else
        A = 25.0f, B = 12.5f, OX = 120.0f, OY = 34.0f;
    backdrop(oy);
    for (i = 0; i < 16; i++) {          /* the floor */
        float gx = (float)(i % 4), gy = (float)(i / 4), dx = gx - (float)(pl.v.pos % 4), dy = gy - (float)(pl.v.pos / 4);
        int live = pl.v.live >> i & 1u;
        pl_pt a = at(gx, gy), b = at(gx + 1, gy), c = at(gx + 1, gy + 1), d = at(gx, gy + 1);
        quad(a, b, c, d, oy, live ? PL_CELL : PL_OUT);
        outline(a, b, c, d, oy, !live ? PL_OUT_EDGE : pulse && dx * dx + dy * dy < 9.0f ? PL_PULSE : PL_EDGE);
    }
    if (pl.v.end >= 0 && pl.v.end < 15) {   /* the loop ends here: an orange edge after its last step */
        float gx = (float)((pl.v.end + 1) % 4), gy = (float)((pl.v.end + 1) / 4);
        pl_pt a = at(gx, gy), b = at(gx, gy + 1);
        line(a, b, oy, PL_ORANGE[1]);
        line((pl_pt){a.x + 1, a.y}, (pl_pt){b.x + 1, b.y}, oy, PL_ORANGE[1]);
    }
    if (hit) {                          /* the floor's square wave */
        float cx = (float)(pl.hit_cell % 4) + 0.5f, cy = (float)(pl.hit_cell / 4) + 0.5f, g = 0.5f + 1.6f * k;
        outline(at(cx - g, cy - g), at(cx + g, cy - g), at(cx + g, cy + g), at(cx - g, cy + g), oy, k < 0.4f ? PL_PINK[1] : PL_PINK[2]);
    }
    for (s = 0; s <= 6; s++)            /* far to near */
        for (i = 0; i < 16; i++)
            if (i % 4 + i / 4 == s && pl.v.live >> i & 1u)
                block(i, oy, held, hit && i == pl.hit_cell, k);
    if (held) {
        held_panel(oy);
        return;
    }
    if (pl.v.npath)
        path(oy);
    if (pl.v.pos >= 0)
        sight(pl.v.pos, 4, hit ? PL_PINK[1] : PL_GREEN[0], hit ? 0.02f : 0.12f, oy);
    else if (pl.v.cursor >= 0)
        sight(pl.v.cursor, 4, PL_TEXT, 0.12f, oy);
    cv_text(8, 4 + oy, &FONT_S, pl.v.top_l, pl.v.pos < 0 ? PL_LABEL : hit ? PL_PINK[1] : PL_GREEN[0]);
    cv_text(232 - text_w(&FONT_S, pl.v.top_r), 4 + oy, &FONT_S, pl.v.top_r, hit ? PL_PINK[1] : PL_LABEL);
}

/* ---- GRIDS: the stacks (spec 5b, 3 bis; docs/screens/backdrops.html, G2) ------------------------------------- */
static const uint16_t PL_BLUE[3] = {RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)};
static const uint16_t *const GVC[3] = {PL_ORANGE, PL_GREEN, PL_BLUE};      /* BD, SN, HH */
#define G_PEND 8

static struct {
    q_gview v;
    uint32_t ms, hit[3];
    int8_t hit_step[3], last_pos;
    uint8_t extra[32];                  /* bit v: voice v played this step this pass above its map's level (CHAOS) */
    struct { uint8_t on, voice; uint32_t land; } pend[G_PEND];
} gr;

void grids_enter(void)
{
    __builtin_memset(gr.pend, 0, sizeof gr.pend);
    __builtin_memset(gr.extra, 0, sizeof gr.extra);
    gr.hit[0] = gr.hit[1] = gr.hit[2] = gr.ms - 100000u;
}

void grids_drop(int voice, uint32_t land_ms)
{
    int i, k = 0;
    for (i = 0; i < G_PEND; i++)        /* a free slot, else the one sounding first */
        if (!gr.pend[i].on) {
            k = i;
            break;
        } else if (gr.pend[i].land - gr.pend[k].land > 0x80000000u) {
            k = i;
        }
    gr.pend[k].on = 1;
    gr.pend[k].voice = (uint8_t)voice;
    gr.pend[k].land = land_ms;
}

static int g_on(const q_gview *v, int voice, int step) { return v->level[voice][step] > 255 - v->dens[voice] * 255 / 127; }

void grids_frame(const q_gview *v, uint32_t ms)
{
    int i;
    if (v->pos >= 0 && v->pos < gr.last_pos)   /* a new pass: CHAOS's extras forgotten */
        __builtin_memset(gr.extra, 0, sizeof gr.extra);
    gr.last_pos = v->pos;
    for (i = 0; i < G_PEND; i++)        /* the hits that sound now land on the step playing */
        if (gr.pend[i].on && (int32_t)(ms - gr.pend[i].land) >= 0) {
            int vc = gr.pend[i].voice;
            gr.pend[i].on = 0;
            if (v->pos < 0 || vc > 2)
                continue;
            gr.hit[vc] = gr.pend[i].land;
            gr.hit_step[vc] = v->pos;
            if (!g_on(v, vc, v->pos))
                gr.extra[v->pos] |= (uint8_t)(1u << vc);
        }
    gr.v = *v;
    gr.ms = ms;
}

void grids_band(int32_t oy)
{
    const q_gview *v = &gr.v;
    const float th = 5.0f, o = (float)oy;
    int i, s, k;
    uint32_t near = 9999u;
    A = 18.5f, B = 9.25f, OX = 83.0f, OY = 42.0f;
    cv_rect(0, oy, 240, 166, PL_BG);
    for (k = -6; k <= 14; k++) {        /* the wire floor, dim, all around */
        line(at((float)k, -8.0f), at((float)k, 12.0f), oy, RGB(0x12, 0x0c, 0x2a));
        line(at(-8.0f, (float)k - 4.0f), at(16.0f, (float)k - 4.0f), oy, RGB(0x12, 0x0c, 0x2a));
    }
    {                                   /* the plate: its shadow and its two front sides */
        pl_pt a0 = at(0, 4), a1 = at(8, 4), a2 = at(8, 0);
        p3d_fill(a0.x, a0.y + th + 6 + o, a1.x, a1.y + th + 6 + o, a2.x, a2.y + th + 6 + o, RGB(0x05, 0x03, 0x0f));
        quad(a0, a1, (pl_pt){a1.x, a1.y + th}, (pl_pt){a0.x, a0.y + th}, oy, RGB(0x22, 0x18, 0x4a));
        quad(a1, a2, (pl_pt){a2.x, a2.y + th}, (pl_pt){a1.x, a1.y + th}, oy, RGB(0x16, 0x0f, 0x36));
    }
    for (i = 0; i < 32; i++) {
        float gx = (float)(i % 8), gy = (float)(i / 8);
        pl_pt a = at(gx, gy), b = at(gx + 1, gy), c = at(gx + 1, gy + 1), d = at(gx, gy + 1);
        quad(a, b, c, d, oy, i % 4 == 0 ? RGB(0x15, 0x10, 0x30) : PL_CELL);
        outline(a, b, c, d, oy, PL_EDGE);
    }
    outline(at(0, 0), at(8, 0), at(8, 4), at(0, 4), oy, PL_PULSE);
    for (k = 0; k < 8; k += 4)          /* the beats, on the back edge */
        line(at((float)k, 0), up(at((float)k, 0), 5), oy, PL_LABEL);
    for (k = 0; k < 4; k++) {           /* the rows' first steps */
        char t[4];
        int n = k * 8 + 1;
        pl_pt p = at(0, (float)k + 0.5f);
        t[0] = (char)(n < 10 ? '0' + n : '0' + n / 10), t[1] = (char)(n < 10 ? 0 : '0' + n % 10), t[2] = 0;
        cv_text((int32_t)p.x - 6 - text_w(&FONT_XS, t), (int32_t)p.y - 7 + oy, &FONT_XS, t, v->pos / 8 == k ? PL_GREEN[0] : PL_LABEL);
    }
    for (k = 0; k < 3; k++)
        if (gr.ms - gr.hit[k] < near)
            near = gr.ms - gr.hit[k];
    for (s = 0; s <= 10; s++)           /* far to near: the stacks, BD at the base */
        for (i = 0; i < 32; i++) {
            if (i % 8 + i / 8 != s)
                continue;
            float gx = (float)(i % 8), gy = (float)(i / 8), z = 0.0f, top;
            pl_pt c = at(gx + 0.5f, gy + 0.5f);
            for (k = 0; k < 3; k++) {
                uint32_t dt = gr.ms - gr.hit[k];
                int hot = dt < HIT_MS && gr.hit_step[k] == i, sel = v->voice == 0 || v->voice - 1 == k;
                float lift = hot ? 4.0f * (1.0f - (float)dt / (float)HIT_MS) : 0.0f;
                const uint16_t *T = GVC[k];
                uint16_t t0 = hot ? PL_PINK[0] : sel ? T[0] : T[1], t1 = hot ? PL_PINK[1] : sel ? T[1] : T[2], t2 = hot ? PL_PINK[2] : sel ? T[2] : RGB(0x16, 0x0f, 0x36);
                if (g_on(v, k, i) || hot) {
                    float h = 4.0f + (float)v->level[k][i] / 255.0f * 9.0f, sx = A * 0.36f, sy = B * 0.36f, y = c.y - z - lift;
                    quad((pl_pt){c.x - sx, y}, (pl_pt){c.x, y + sy}, (pl_pt){c.x, y + sy - h}, (pl_pt){c.x - sx, y - h}, oy, t1);
                    quad((pl_pt){c.x, y + sy}, (pl_pt){c.x + sx, y}, (pl_pt){c.x + sx, y - h}, (pl_pt){c.x, y + sy - h}, oy, t2);
                    quad((pl_pt){c.x, y - sy - h}, (pl_pt){c.x + sx, y - h}, (pl_pt){c.x, y + sy - h}, (pl_pt){c.x - sx, y - h}, oy, t0);
                    outline((pl_pt){c.x, y - sy - h}, (pl_pt){c.x + sx, y - h}, (pl_pt){c.x, y + sy - h}, (pl_pt){c.x - sx, y - h}, oy, t2);
                    z += h;
                } else if (gr.extra[i] >> k & 1u) {   /* played by CHAOS: an outline */
                    float y = c.y - z;
                    outline((pl_pt){c.x - 6, y - 2}, (pl_pt){c.x, y - 5}, (pl_pt){c.x + 6, y - 2}, (pl_pt){c.x, y + 1}, oy, T[0]);
                    z += 4.0f;
                }
            }
            top = c.y - z;
            if (i == v->pos && near < 300u) {          /* the hit: a square wave on the floor, shards from the top */
                float kk = (float)near / 300.0f, g = 0.5f + kk * 1.4f;
                outline(at(gx + 0.5f - g, gy + 0.5f - g), at(gx + 0.5f + g, gy + 0.5f - g), at(gx + 0.5f + g, gy + 0.5f + g), at(gx + 0.5f - g, gy + 0.5f + g), oy,
                        kk < 0.4f ? PL_PINK[1] : PL_PINK[2]);
                for (k = 0; k < 3; k++) {
                    float sx = c.x + (float)(k - 1) * 9.0f * kk * 1.4f, sy = top - 6.0f - kk * 20.0f - (float)k * 3.0f + o, z2 = 3.0f * (1.0f - kk) + 1.0f;
                    p3d_fill(sx, sy - z2, sx + z2, sy + z2 * 0.4f, sx - z2 * 0.6f, sy + z2, k % 2 ? PL_PINK[0] : PL_PINK[1]);
                }
            }
        }
    if (v->pos >= 0)
        sight(v->pos, 8, near < HIT_MS ? PL_PINK[1] : PL_GREEN[0], near < HIT_MS ? 0.02f : 0.12f, oy);
    for (k = 0; k < 3; k++) {           /* the densities: a thin line each, BD on top */
        cv_rect(196, oy + 6 + 6 * k, 36, 2, RGB(0x1c, 0x16, 0x38));
        cv_rect(196, oy + 6 + 6 * k, 36 * v->dens[k] / 127, 2, gr.ms - gr.hit[k] < HIT_MS ? PL_PINK[1] : GVC[k][1]);
    }
    {                                   /* the map's place: 5 x 5 dots, the cursor shaking with CHAOS */
        float j = (float)v->chaos / 127.0f * 1.5f * fm_sinf((float)(gr.ms % 100000u) * 0.009f), cx = 12.0f + (float)v->x / 127.0f * 16.0f + j + 1.0f;
        float cy = 138.0f + (float)v->y / 127.0f * 14.0f + 1.0f + o;
        for (i = 0; i < 5; i++)
            for (k = 0; k < 5; k++)
                cv_rect(12 + i * 4, oy + 138 + (int32_t)((float)k * 3.5f), 1, 1, RGB(0x3a, 0x2c, 0x64));
        p3d_fill(cx - 3, cy, cx, cy - 3, cx + 3, cy, PL_TEXT);
        p3d_fill(cx - 3, cy, cx + 3, cy, cx, cy + 3, PL_TEXT);
    }
    cv_text(8, 4 + oy, &FONT_S, v->top_l, v->pos < 0 ? PL_LABEL : near < HIT_MS ? PL_PINK[1] : PL_GREEN[0]);
    cv_text(8, 20 + oy, &FONT_XS, v->top_r, PL_LABEL);
}

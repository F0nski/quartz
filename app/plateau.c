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

static void backdrop(int32_t oy)
{
    float t = (float)(pl.ms % 3600000u) * 0.001f, f = 0.5f * t - fm_floorf(0.5f * t);
    int k;
    cv_rect(0, oy, 240, 166, PL_BG);
    for (k = -8; k <= 8; k++)
        cv_line(120, oy + 4, 120 + 40 * k, oy + 166, PL_RAY);
    for (k = 0; k < 6; k++) {
        float p = ((float)k + f) / 6.0f;
        cv_rect(0, oy + 4 + (int32_t)(162.0f * p * p), 240, 1, PL_RAY);
    }
    for (k = 0; k < 2; k++) {           /* two wire cubes drifting high up */
        float cx = k ? 212.0f : 28.0f, cy = k ? 16.0f : 20.0f, a = t * 0.6f + 2.0f * (float)k;
        pl_pt q[4];
        int i;
        for (i = 0; i < 4; i++) {
            float b = a + (float)i * (FM_TWO_PI / 4.0f);
            q[i] = (pl_pt){cx + fm_cosf(b) * 7.0f, cy + fm_sinf(b) * 3.5f};
        }
        for (i = 0; i < 4; i++) {
            line(q[i], q[(i + 1) % 4], oy, PL_CUBE);
            line(up(q[i], 9), up(q[(i + 1) % 4], 9), oy, PL_CUBE);
            line(q[i], up(q[i], 9), oy, PL_CUBE);
        }
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

static void sight(int cell, uint16_t col, float g, int32_t oy)
{
    float gx = (float)(cell % 4), gy = (float)(cell / 4);
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
        sight(pl.v.pos, hit ? PL_PINK[1] : PL_GREEN[0], hit ? 0.02f : 0.12f, oy);
    else if (pl.v.cursor >= 0)
        sight(pl.v.cursor, PL_TEXT, 0.12f, oy);
    cv_text(8, 4 + oy, &FONT_S, pl.v.top_l, pl.v.pos < 0 ? PL_LABEL : hit ? PL_PINK[1] : PL_GREEN[0]);
    cv_text(232 - text_w(&FONT_S, pl.v.top_r), 4 + oy, &FONT_S, pl.v.top_r, hit ? PL_PINK[1] : PL_LABEL);
}

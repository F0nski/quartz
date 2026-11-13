/* QUARTZ generators: the drum map of Grids and Marbles' random rhythms and notes, one step at a time.
 *
 * Copyright (C) the QUARTZ authors. License: GPL-3.0-or-later.
 * The Grids part is a port of Mutable Instruments Grids (Emilie Gillet, GPL-3.0-or-later; the drum map is in
 * third_party/grids/map.h). The Marbles part is a port of Mutable Instruments Marbles (Emilie Gillet, MIT; the
 * distribution tables are in third_party/marbles/tables.h). No libc headers: the app's C is built freestanding. */
#include "gen.h"
#include "third_party/grids/map.h"
#include "third_party/marbles/tables.h"

#define GRIDS_STEPS 32

/* the same xorshift as the sequencer's (seq_rand); gen does not depend on seq.c */
static uint32_t gen_rand(uint32_t *rng)
{
    *rng ^= *rng << 13;
    *rng ^= *rng >> 17;
    *rng ^= *rng << 5;
    return *rng;
}

/* 0..1, below 1: 24 bits */
static float gen_float(uint32_t *rng) { return (gen_rand(rng) >> 8) * (1.0f / 16777216.0f); }

void gen_marbles_draw(q_gen *g, uint32_t *rng)
{
    for (int i = 0; i < 16; i++) { g->t.loop[i] = gen_float(rng); g->x.loop[i] = gen_float(rng); }
}

void gen_reset(q_gen *g, uint32_t *rng)
{
    __builtin_memset(g, 0, sizeof *g);
    gen_marbles_draw(g, rng);
    g->drum_step = 8;          /* the first step draws a drum pattern */
}

int gen_loop(const q_pattern *pt)
{
    return pt->src == SRC_MARBLES ? pt->marbles.len : pt->src == SRC_LIFE || pt->src == SRC_LANES || pt->src == SRC_CHORDS || pt->src == SRC_TONNETZ || pt->src == SRC_ORBITS ? LIFE_W : GRIDS_STEPS;
}

/* settings 0..127 as Grids' 0..255 */
static uint8_t grids_u8(int v) { return (uint8_t)(v * 255 / 127); }

static uint8_t grids_mix(uint8_t a, uint8_t b, uint8_t bal)
{
    return (uint8_t)((a * (255 - bal) + b * bal) >> 8);
}

/* PatternGenerator::ReadDrumMap: the four nodes around (x, y), interpolated. The 8-bit truncation of x << 2 and
   y << 2 is the original's: the balance within the cell. */
static uint8_t grids_read(int step, int inst, uint8_t x, uint8_t y)
{
    static const uint8_t *const MAP[5][5] = {
        { node_10, node_8, node_0, node_9, node_11 },
        { node_15, node_7, node_13, node_12, node_6 },
        { node_18, node_14, node_4, node_5, node_3 },
        { node_23, node_16, node_21, node_1, node_2 },
        { node_24, node_19, node_17, node_20, node_22 },
    };
    int i = x >> 6, j = y >> 6, o = inst * GRIDS_STEPS + step;
    uint8_t bx = (uint8_t)(x << 2), by = (uint8_t)(y << 2);
    return grids_mix(grids_mix(MAP[i][j][o], MAP[i + 1][j][o], bx),
                     grids_mix(MAP[i][j + 1][o], MAP[i + 1][j + 1][o], bx), by);
}

int gen_grids_level(const q_grids *g, int step, int voice) { return grids_read(step % GRIDS_STEPS, voice, grids_u8(g->x), grids_u8(g->y)); }

/* PatternGenerator::EvaluateDrums */
static int grids_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st)
{
    const q_grids *gr = &pt->grids;
    int step = k % GRIDS_STEPS, acc = 0, n = 0;
    if (step == 0)
        for (int i = 0; i < 3; i++)
            g->perturb[i] = (uint8_t)(((gen_rand(rng) & 255) * (grids_u8(gr->chaos) >> 2)) >> 8);
    int kit = p->sound[track].model == M_KIT;
    for (int i = 0; i < 3; i++) {
        int level = grids_read(step, i, grids_u8(gr->x), grids_u8(gr->y)) + g->perturb[i];
        if (level > 255) level = 255;
        if (level <= 255 - grids_u8(gr->dens[i])) continue;
        if (!kit && i != gr->voice - 1) continue;
        if (level > 192) acc |= 1 << n;
        st->note[n++] = kit ? KIT_NOTE + i : gr->note;
    }
    if (n) { st->flags |= SF_ON; st->vel = 95; st->len = 4; st->prob = 20; }
    return acc;
}

/* ---- MARBLES ---- */

const uint16_t SCALE_MASK[11] = {
    0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0x295, 0x4A9,
};

uint16_t gen_scale(const q_project *p)
{
    uint16_t m = SCALE_MASK[p->mode];
    return (uint16_t)(((m << p->tonic) | (m >> (12 - p->tonic))) & 0xFFF);
}

uint16_t gen_allowed(const q_project *p, const q_pattern *pt)
{
    uint16_t m = (uint16_t)((pt->marbles.notes[0] | pt->marbles.notes[1] << 8) & 0xFFF);
    return m ? m : gen_scale(p);
}

uint16_t gen_kept(uint16_t allowed, int tonic, int steps)
{
    static const uint8_t STRENGTH[12] = { 0, 7, 4, 3, 10, 11, 2, 5, 9, 8, 1, 6 };
    int n = 0;
    for (int i = 0; i < 12; i++) n += allowed >> i & 1;
    int keep = n - steps * (n - 1) / 127, kept = 0;
    for (int i = 0; i < 12 && keep; i++) {
        int c = (tonic + STRENGTH[i]) % 12;
        if (allowed >> c & 1) { kept |= 1 << c; keep--; }
    }
    return (uint16_t)kept;
}

int gen_quantize(int note, uint16_t allowed, int tonic, int steps)
{
    int kept = gen_kept(allowed, tonic, steps);
    int best = 0, dist = 1 << 30;
    for (int c = 1; c < 128; c++) {       /* upward: a tie keeps the lower note. From 1: note 0 is no note */
        int d = c > note ? c - note : note - c;
        if ((kept >> (c % 12) & 1) && d < dist) { best = c; dist = d; }
    }
    return best;
}

static int gen_round(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

/* stmlib::Interpolate */
static float interpolate(const float *table, float index, float size)
{
    index *= size;
    int i = (int)index;
    return table[i] + (table[i + 1] - table[i]) * (index - i);
}

/* BetaDistributionSample (distributions.h) */
static float beta_sample(float uniform, float spread, float bias)
{
    int flip = bias > 0.5f;
    if (flip) { uniform = 1.0f - uniform; bias = 1.0f - bias; }
    bias *= (5 - 1) * 2.0f;
    spread *= 9 - 1;
    int bi = (int)bias, si = (int)spread;
    float bf = bias - bi, sf = spread - si;
    int cell = bi * (9 + 1) + si, offset = 0;
    /* the lower and upper 5 % use finer tables */
    if (uniform <= 0.05f) { offset = 128 + 1; uniform *= 20.0f; }
    else if (uniform >= 0.95f) { offset = 2 * (128 + 1); uniform = (uniform - 0.95f) * 20.0f; }
    float x1y1 = interpolate(distributions_table[cell] + offset, uniform, 128);
    float x2y1 = interpolate(distributions_table[cell + 1] + offset, uniform, 128);
    float x1y2 = interpolate(distributions_table[cell + 9 + 1] + offset, uniform, 128);
    float x2y2 = interpolate(distributions_table[cell + 9 + 2] + offset, uniform, 128);
    float y1 = x1y1 + (x2y1 - x1y1) * sf;
    float y2 = x1y2 + (x2y2 - x1y2) * sf;
    float y = y1 + (y2 - y1) * bf;
    return flip ? 1.0f - y : y;
}

/* RandomSequence::NextValue, without the shift register mode */
static float dejavu_next(q_dejavu *s, int len, float d, uint32_t *rng)
{
    float p = (2.0f * d - 1.0f) * (2.0f * d - 1.0f);
    int mutate = gen_float(rng) < p;
    if (mutate && d <= 0.5f) {
        s->loop[s->head] = gen_float(rng);
        s->head = (s->head + 1) % 16;
        s->step = len - 1;
    } else if (mutate) {
        s->step = (int)(gen_float(rng) * len);
    } else {
        s->step = (s->step + 1) % len;
    }
    return s->loop[(s->head + 16 - len + s->step) % 16];
}

static const uint8_t DIVIDER_PATTERNS[17][2][2] = {
    { { 1, 1 }, { 1, 1 } }, { { 1, 1 }, { 2, 1 } }, { { 1, 2 }, { 1, 1 } }, { { 1, 1 }, { 4, 1 } },
    { { 1, 2 }, { 2, 1 } }, { { 1, 1 }, { 3, 2 } }, { { 1, 4 }, { 4, 1 } }, { { 1, 4 }, { 2, 1 } },
    { { 1, 2 }, { 3, 2 } }, { { 1, 1 }, { 8, 1 } }, { { 1, 1 }, { 3, 1 } }, { { 1, 3 }, { 1, 1 } },
    { { 1, 1 }, { 5, 4 } }, { { 1, 2 }, { 5, 4 } }, { { 1, 1 }, { 6, 1 } }, { { 1, 3 }, { 2, 1 } },
    { { 1, 1 }, { 16, 1 } },
};
static const uint8_t DIVIDER_LEN[17] = { 1, 1, 2, 1, 2, 2, 4, 4, 2, 1, 1, 3, 4, 4, 1, 3, 1 };
static const uint8_t FIXED_DIVIDER_PATTERNS[17][2][2] = {
    { { 8, 1 }, { 1, 8 } }, { { 6, 1 }, { 1, 6 } }, { { 4, 1 }, { 1, 4 } }, { { 3, 1 }, { 1, 3 } },
    { { 2, 1 }, { 1, 2 } }, { { 3, 2 }, { 2, 3 } }, { { 4, 3 }, { 3, 4 } }, { { 5, 4 }, { 4, 5 } },
    { { 1, 1 }, { 1, 1 } },
    { { 4, 5 }, { 5, 4 } }, { { 3, 4 }, { 4, 3 } }, { { 2, 2 }, { 3, 2 } }, { { 1, 2 }, { 2, 1 } },
    { { 1, 3 }, { 3, 1 } }, { { 1, 4 }, { 4, 1 } }, { { 1, 6 }, { 6, 1 } }, { { 1, 8 }, { 8, 1 } },
};
static const uint8_t FIXED_DIVIDER_LEN[17] = { 8, 6, 4, 3, 2, 6, 12, 20, 1, 20, 12, 6, 2, 3, 4, 6, 8 };

static const uint8_t DRUM_PATTERNS[18][8] = {
    { 1, 0, 0, 0, 2, 0, 0, 0 }, { 0, 0, 1, 0, 2, 0, 0, 0 },
    { 1, 0, 1, 0, 2, 0, 0, 0 }, { 0, 0, 1, 0, 2, 0, 0, 2 },
    { 1, 0, 1, 0, 2, 0, 1, 0 }, { 0, 2, 1, 0, 2, 0, 0, 2 },
    { 1, 0, 0, 0, 2, 0, 1, 0 }, { 0, 2, 1, 0, 2, 0, 1, 2 },
    { 1, 0, 0, 1, 2, 0, 0, 0 }, { 0, 2, 1, 1, 2, 0, 1, 2 },
    { 1, 0, 0, 1, 2, 0, 1, 0 }, { 0, 2, 1, 1, 2, 2, 1, 2 },
    { 1, 0, 0, 1, 2, 0, 1, 2 }, { 0, 2, 0, 1, 2, 0, 1, 2 },
    { 1, 0, 1, 1, 2, 0, 1, 2 }, { 2, 0, 1, 2, 0, 1, 2, 0 },
    { 1, 2, 1, 1, 2, 0, 1, 2 }, { 2, 0, 1, 2, 0, 1, 2, 2 },
};

enum { T_COMPLEMENTARY_BERNOULLI, T_CLUSTERS, T_DRUMS, T_INDEPENDENT_BERNOULLI, T_DIVIDER, T_THREE_STATES, T_MARKOV };

/* TGenerator::GenerateMarkov */
static int markov(q_gen *g, float bias, float d, int len, const float *u, float pr)
{
    int bits = 0, p = g->markov_ptr;
    float b = 1.5f * bias - 0.5f;
    g->markov[p] = 0;
    for (int i = 0; i < 2; i++) {
        int mask = 1 << i;
        int periodic = g->markov[(p + 8) % 16] & mask;
        int simultaneous = g->markov[(p + 8) % 16] & ~mask;
        int dense = g->markov[(p + 1) % 16] & mask;
        int alternate = g->markov[(p + 4) % 16] & ~mask;
        float logit = -1.5f;
        logit += g->streak[i] > 24 ? 10.0f : 0.0f;
        logit += 8.0f * __builtin_fabsf(b) * (periodic ? b : -b);
        logit -= 2.0f * (simultaneous ? b : -b);
        logit -= 1.0f * (dense ? b : 0.0f);
        logit += 1.0f * (alternate ? b : 0.0f);
        if (logit < -10.0f) logit = -10.0f;
        if (logit > 10.0f) logit = 10.0f;
        int state = u[i] < 1.0f / (1.0f + __builtin_expf(-logit));
        if (d >= pr) state = g->markov[(p + len) % 16] & mask;
        if (state) { bits |= mask; g->streak[i] = 0; }
        else g->streak[i]++;
    }
    g->markov[p] |= (uint8_t)bits;
    g->markov_ptr = (p + 16 - 1) % 16;
    return bits;
}

/* TGenerator's DIVIDER and CLUSTERS: the pulses of t1 and t3 at this step, in rat[] (0 = silent) */
static void divider(q_gen *g, int clusters, float bias, float u0, int *rat)
{
    if (--g->div_left <= 0) {
        int len;
        if (!clusters) {
            int i = (int)(bias * 17);
            if (i > 16) i = 16;
            __builtin_memcpy(g->div_pat, FIXED_DIVIDER_PATTERNS[i], sizeof g->div_pat);
            len = FIXED_DIVIDER_LEN[i];
        } else {
            float strength = __builtin_fabsf(bias - 0.5f) * 2.0f, u = u0;
            u *= (u + strength * strength * (1.0f - u));
            u *= strength;
            int i = (int)(u * 17);
            int sw = bias < 0.5f;
            __builtin_memcpy(g->div_pat[0], DIVIDER_PATTERNS[i][sw], 2);
            __builtin_memcpy(g->div_pat[1], DIVIDER_PATTERNS[i][!sw], 2);
            len = DIVIDER_LEN[i];
        }
        g->div_left = len;
        g->div_n = 0;
    }
    int n = g->div_n++;
    for (int i = 0; i < 2; i++) {
        int p = g->div_pat[i][0], q = g->div_pat[i][1];
        int c = ((n + 1) * p + q - 1) / q - (n * p + q - 1) / q;   /* ceilings: a 1/q ratio fires on step 0 */
        rat[i] = c > 4 ? 4 : c;
    }
}

/* One clock of TGenerator (t1 bit 0, t3 bit 1; t2 is the clock) and, when the step plays, one draw of
   OutputChannel::GenerateNewVoltage. */
static int marbles_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, uint32_t *rng, q_step *st)
{
    const q_marbles *m = &pt->marbles;
    /* 63 and 64 both mean the centre: the loop is frozen there, never mutated */
    float bias = m->bias / 127.0f, d = m->dejavu == 63 || m->dejavu == 64 ? 0.5f : m->dejavu / 127.0f;
    int len = m->len;

    /* RandomSequence::NextVector: pulse widths (unused), u[0], u[1], p, jitter. 24 bits keep them below 1. */
    float x[6];
    uint32_t w = (uint32_t)(dejavu_next(&g->t, len, d, rng) * 4294967296.0f);
    for (int i = 0; i < 6; i++) { x[i] = (w >> 8) * (1.0f / 16777216.0f); w = w * 1664525u + 1013904223u; }
    const float *u = x + 2;
    /* X draws on every step, even when its note is not played, so that T and X loop together */
    float xu = dejavu_next(&g->x, len, d, rng);

    int bits = 0, rat[2] = { 1, 1 };
    switch (m->mode) {
    case T_COMPLEMENTARY_BERNOULLI:
        bits = u[0] > bias ? 1 : 2;
        break;
    case T_INDEPENDENT_BERNOULLI:
        bits = (u[0] > bias) | (!(u[1] > bias)) << 1;
        break;
    case T_THREE_STATES: {
        float p_none = 0.75f - __builtin_fabsf(bias - 0.5f);
        float threshold = p_none + (1.0f - p_none) * (0.25f + bias * 0.5f);
        if (u[0] > p_none) bits = u[0] > threshold ? 1 : 2;
        break;
    }
    case T_DRUMS:
        if (++g->drum_step >= 8) {
            g->drum_step = 0;
            float du = u[0] * 2.0f * __builtin_fabsf(bias - 0.5f);
            g->drum_index = (int)(18 * du);
            if (bias <= 0.5f) g->drum_index -= g->drum_index % 2;
        }
        bits = DRUM_PATTERNS[g->drum_index][g->drum_step];
        break;
    case T_MARKOV:
        bits = markov(g, bias, d, len, u, x[4]);
        break;
    default:
        divider(g, m->mode == T_CLUSTERS, bias, u[0], rat);
        bits = (rat[0] > 0) | (rat[1] > 0) << 1;
        break;
    }

    int ratchet = 1, n = 0;
    if (p->sound[track].model == M_KIT) {
        /* t1, t2, t3 on pads 1, 2, 3. A step has one ratchet: when t1 and t3 both ratchet, the larger count. */
        if (bits & 1) { st->note[n++] = KIT_NOTE; ratchet = rat[0]; }
        st->note[n++] = KIT_NOTE + 1;
        if (bits & 2) { st->note[n++] = KIT_NOTE + 2; if (rat[1] > ratchet) ratchet = rat[1]; }
    } else {
        int out = m->tout == 2 ? 1 : bits & (m->tout == 1 ? 1 : 2);
        if (!out) return 0;
        if (m->tout != 2) ratchet = rat[m->tout == 3];
        float spread = m->spread / 127.0f, xbias = m->xbias / 127.0f;
        float degenerate = 1.25f - spread * 25.0f, bernoulli = spread * 25.0f - 23.75f;
        degenerate = degenerate < 0.0f ? 0.0f : degenerate > 1.0f ? 1.0f : degenerate;
        bernoulli = bernoulli < 0.0f ? 0.0f : bernoulli > 1.0f ? 1.0f : bernoulli;
        float v = beta_sample(xu, spread, xbias);
        v += degenerate * (xbias - v);
        v += bernoulli * ((xu >= 1.0f - xbias ? 0.999999f : 0.0f) - v);
        st->note[n++] = (uint8_t)gen_quantize(m->note + gen_round(v * m->range * 12), gen_allowed(p, pt), p->tonic,
                                              m->steps);
    }
    float j = m->jitter / 127.0f;
    st->flags |= SF_ON | (uint8_t)((ratchet - 1) << 3);
    st->vel = 100; st->len = 4; st->prob = 20;
    st->micro = (int8_t)gen_round((2.0f * x[5] - 1.0f) * j * j * j * j * 11.0f);
    return 0;      /* Marbles has no accent */
}

/* LIFE: Conway's Game of Life on a 16 x 8 torus, read a column per step */

static uint16_t rotl16(uint16_t v, int n) { return (uint16_t)(v << n | v >> (16 - n)); }

/* B3/S23: a cell is born with 3 living neighbours, survives with 2 or 3 */
static void life_next(const uint16_t in[LIFE_H], uint16_t out[LIFE_H])
{
    for (int r = 0; r < LIFE_H; r++) {
        uint16_t nb[8], row[3] = { in[(r + LIFE_H - 1) % LIFE_H], in[r], in[(r + 1) % LIFE_H] };
        int n = 0;
        for (int i = 0; i < 3; i++) {
            nb[n++] = rotl16(row[i], 1);
            nb[n++] = rotl16(row[i], 15);
            if (i != 1) nb[n++] = row[i];
        }
        uint16_t res = 0;
        for (int c = 0; c < LIFE_W; c++) {
            int cnt = 0;
            for (int i = 0; i < 8; i++) cnt += nb[i] >> c & 1;
            if (cnt == 3 || (cnt == 2 && (in[r] >> c & 1))) res |= (uint16_t)(1u << c);
        }
        out[r] = res;
    }
}

/* a grid drawn with the run's own xorshift; a cell lives with probability dens / 127 x 0.55 */
static void life_draw(q_life_run *l)
{
    uint32_t lim = (uint32_t)l->dens * 72657u;      /* 0.55 x 2^24 / 127 */
    for (int r = 0; r < LIFE_H; r++) {
        uint16_t v = 0;
        for (int c = 0; c < LIFE_W; c++)
            if ((gen_rand(&l->rng) >> 8) < lim) v |= (uint16_t)(1u << c);
        l->grid[r] = v;
    }
    __builtin_memcpy(l->born, l->grid, sizeof l->born);
    __builtin_memset(l->prev, 0, sizeof l->prev);
    __builtin_memset(l->mut, 0, sizeof l->mut);
    l->stuck = 0;
}

void gen_life_seed(q_life_run *l, int seed, int dens)
{
    l->seed = (uint8_t)seed, l->dens = (uint8_t)dens, l->seeded = 1;
    l->rng = ((uint32_t)seed * 2654435761u ^ (uint32_t)dens * 40503u ^ 0x9e3779b9u) | 1;
    life_draw(l);
}

/* NOTE in the scale, then r degrees of the scale up; above 127, the scale's last note below */
int gen_life_note(int note, uint16_t scale, int tonic, int r)
{
    int n = gen_quantize(note, scale, tonic, 0);
    while (r > 0) {
        int m = n + 1;
        while (m <= 127 && !(scale >> (m % 12) & 1)) m++;
        if (m > 127) break;
        n = m, r--;
    }
    return n;
}

int gen_life_rows(const q_life *l, int kit, const uint16_t grid[LIFE_H], int col, uint8_t rows[4])
{
    int cap = kit && l->poly > 3 ? 3 : l->poly, n = 0, band = -1;
    for (int r = 0; r < LIFE_H && n < cap; r++) {
        if (!(grid[r] >> col & 1)) continue;
        if (kit && l->map) {
            int b = r < 3 ? 0 : r < 6 ? 1 : 2;
            if (b == band) continue;
            band = b;
        }
        rows[n++] = (uint8_t)r;
    }
    return n;
}

static void life_generation(q_life_run *l, const q_life *s)
{
    uint16_t next[LIFE_H];
    life_next(l->grid, next);
    int empty = 1, same0 = 1, same1 = 1;
    for (int r = 0; r < LIFE_H; r++) {
        empty &= !next[r];
        same0 &= next[r] == l->grid[r];
        same1 &= next[r] == l->prev[0][r];
    }
    __builtin_memset(l->mut, 0, sizeof l->mut);
    for (int i = (s->mut * 6 + 63) / 127; i > 0; i--) {
        int c = (int)(gen_rand(&l->rng) % LIFE_W), r = (int)(gen_rand(&l->rng) % LIFE_H);
        next[r] |= (uint16_t)(1u << c);
        l->mut[r] |= (uint16_t)(1u << c);
    }
    l->gens++;
    if (!s->mut && (empty || same0 || same1)) {
        if (s->autos) {                 /* the run's next draw: SEED stays */
            l->reseeds++;
            life_draw(l);
            return;
        }
        l->stuck = 1;
    } else {
        l->stuck = 0;
    }
    __builtin_memcpy(l->prev[1], l->prev[0], sizeof l->prev[1]);
    __builtin_memcpy(l->prev[0], l->grid, sizeof l->prev[0]);
    for (int r = 0; r < LIFE_H; r++) l->born[r] = (uint16_t)(next[r] & ~l->grid[r]);
    __builtin_memcpy(l->grid, next, sizeof l->grid);
}

static int life_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, q_step *st)
{
    q_life_run *l = &g->life;
    const q_life *s = &pt->life;
    if (!l->seeded) {
        gen_life_seed(l, s->seed, s->dens);
    } else if (k == 0) {
        if (l->seed != s->seed || l->dens != s->dens) {
            l->reseeds++;
            gen_life_seed(l, s->seed, s->dens);
            l->tours = 0;
        } else if (++l->tours >= 1 << s->gen) {
            l->tours = 0;
            life_generation(l, s);
        }
    }
    int kit = p->sound[track].model == M_KIT, acc = 0;
    uint8_t rows[4];
    int n = gen_life_rows(s, kit, l->grid, k, rows);
    if (!n) return 0;
    for (int i = 0; i < n; i++) {
        int r = rows[i];
        st->note[i] = (uint8_t)(!kit ? gen_life_note(s->note, gen_scale(p), p->tonic, r)
                                     : KIT_NOTE + (s->map ? (r < 3 ? 0 : r < 6 ? 1 : 2) : r));
        if (l->born[r] >> k & 1) acc |= 1 << i;
    }
    st->flags |= SF_ON;
    st->vel = 95; st->len = 4; st->prob = 20;
    return acc;
}

/* LANES */

int gen_lanes_degree(int note, uint16_t scale, int tonic, int pitch)
{
    int best = 0, dist = 1000;
    for (int d = 0; d < 16; d++) {
        int n = gen_life_note(note, scale, tonic, d), e = n > pitch ? n - pitch : pitch - n;
        if (e < dist) best = d, dist = e;           /* strictly nearer: a tie keeps the lower */
    }
    return best;
}

/* the step lanes_n: each lane's head is lanes_n mod its length; the heads never go back at the tour's end */
static int lanes_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, q_step *st)
{
    static const uint8_t LEN[5] = { 2, 4, 8, 16, 32 }, VEL[4] = { 40, 70, 100, 100 };
    const q_lanes *l = &pt->lanes;
    uint32_t c = g->lanes_n++;
    int v[L_N];
    for (int i = 0; i < L_N; i++) v[i] = lanes_get(l, i, (int)(c % (uint32_t)lanes_len(l, i)));
    if (!v[L_TRIG]) return 0;
    int n;
    if (p->sound[track].model == M_KIT) n = KIT_NOTE + v[L_NOTE];
    else {
        n = gen_life_note(l->note, gen_scale(p), p->tonic, v[L_NOTE]);
        for (int o = v[L_OCT] - 2; o > 0 && n + 12 <= 127; o--) n += 12;   /* an octave out of range: the one that fits */
        for (int o = v[L_OCT] - 2; o < 0 && n - 12 >= 1; o++) n -= 12;
    }
    st->flags = SF_ON; st->note[0] = (uint8_t)n;
    st->len = LEN[v[L_DUR]]; st->vel = VEL[v[L_VEL]]; st->prob = 20;
    return v[L_VEL] == 3;
}

/* CHORDS */

/* SMOOTH: t[0..n-1] (ascending) moved to the inversion and octave nearest the last strike, the bass within a fifth of t[0]'s
   (no drift loop after loop); a tie: the lower. CHORDS and TONNETZ. */
static void voice_smooth(int t[4], int n, const uint8_t *prev, int nprev)
{
    int i, j;
    int best[4], cost = 1 << 30, sum = 0;
    for (int inv = 0; inv < n; inv++)
        for (int o = -2; o <= 1; o++) {
            int v[4], ok = 1, cs = 0, s = 0;
            int low = 127;
            for (i = 0; i < n; i++) {
                v[i] = t[i] + (i < inv ? 12 : 0) + 12 * o;
                ok &= v[i] >= 1 && v[i] <= 127;
                if (v[i] < low) low = v[i];
            }
            ok &= low >= t[0] - 7 && low <= t[0] + 7;   /* the bass within a fifth of the root: no drift loop after loop */
            if (!ok) continue;
            for (i = 0; i < n; i++) {
                int e = 1000;
                for (j = 0; j < nprev; j++) { int x = v[i] > prev[j] ? v[i] - prev[j] : prev[j] - v[i]; if (x < e) e = x; }
                cs += e, s += v[i];
            }
            if (cs < cost || (cs == cost && s < sum)) {
                cost = cs, sum = s;
                for (i = 0; i < n; i++) best[i] = v[i];
            }
        }
    if (cost < 1 << 30)
        for (i = 0; i < n; i++) t[i] = best[i];
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && t[j - 1] > t[j]; j--) { int x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
}

int gen_chord_notes(const q_project *p, const q_chords *c, int slot, int poly, const uint8_t *prev, int nprev, uint8_t out[4])
{
    static const uint8_t STACK[CS_N][4] = {{0, 2, 4, 0}, {0, 2, 4, 6}, {0, 1, 4, 0}, {0, 3, 4, 0}};
    static const uint8_t KEEP[4] = {0, 1, 3, 2};             /* under POLY: root, third (or sus), seventh, fifth */
    int ch = c->ch[slot], d = CH_DEG(ch), sz = CH_SIZE(ch), m = sz == CS_SEVENTH ? 4 : 3, n = 0, t[4], i, j;
    uint16_t sc = gen_scale(p);
    for (i = 0; i < 4 && n < poly; i++)
        if (KEEP[i] < m) t[n++] = gen_life_note(c->note, sc, p->tonic, d + STACK[sz][KEEP[i]]);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && t[j - 1] > t[j]; j--) { int x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    if (c->voice && nprev)
        voice_smooth(t, n, prev, nprev);
    for (i = 0; i < n; i++) out[i] = (uint8_t)t[i];
    return n;
}

/* a strike of nn notes (ascending) into st, remembered in last: held until the next strike, STRUM in flags, the bar's first
   strike accented on every note. CHORDS and TONNETZ. */
static int strike_fill(q_step *st, const uint8_t *out, int nn, int rate, int strum, int step, uint8_t last[4], uint8_t *nlast)
{
    for (int i = 0; i < nn; i++) st->note[i] = last[i] = out[i];
    *nlast = (uint8_t)nn;
    st->flags = (uint8_t)(SF_ON | strum << 5);
    st->len = (uint8_t)(128 >> rate); st->vel = 100; st->prob = 20;
    return step == 0 ? (1 << nn) - 1 : 0;
}

/* the step chords.n: a strike on every 16 / strikes step of the bar, the chord (n / 16) mod LEN */
static int chords_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, q_step *st)
{
    const q_chords *c = &pt->chords;
    if (!g->chords.n)                   /* the source set mid-tour: its bars line up with the tour */
        g->chords.n = (uint32_t)k;
    uint32_t n = g->chords.n++;
    int step = (int)(n % 16), every = 16 >> c->rate, poly = p->sound[track].poly ? p->sound[track].poly : 1;
    if (p->sound[track].model == M_KIT || step % every)
        return 0;
    uint8_t out[4];
    int nn = gen_chord_notes(p, c, (int)(n / 16 % c->len), poly, g->chords.last, g->chords.nlast, out);
    return strike_fill(st, out, nn, c->rate, c->strum, step, g->chords.last, &g->chords.nlast);
}

/* TONNETZ */

static int tz_pc(int i, int j) { return ((7 * i + 4 * j) % 12 + 12) % 12; }

q_tz gen_tz_move(q_tz s, int op)
{
    if (op == TZ_P) s.j = (int16_t)(s.j + (s.m ? 1 : -1));
    else if (op == TZ_R) s.i = (int16_t)(s.i + (s.m ? 1 : -1));
    if (op != TZ_STAY) s.m ^= 1;
    return s;
}

void gen_tz_pcs(q_tz s, int base, uint8_t pcs[3])
{
    int r = s.m ? tz_pc(s.i, s.j + 1) : tz_pc(s.i, s.j);
    pcs[0] = (uint8_t)((r + base + 24) % 12);
    pcs[1] = (uint8_t)((r + (s.m ? 3 : 4) + base + 24) % 12);
    pcs[2] = (uint8_t)((r + 7 + base + 24) % 12);
}

static int tz_in_scale(q_tz s, uint16_t scale, int base)
{
    uint8_t x[3];
    gen_tz_pcs(s, base, x);
    return (scale >> x[0] & 1) && (scale >> x[1] & 1) && (scale >> x[2] & 1);
}

q_tz gen_tz_step(q_tz s, int op, int in_scale, uint16_t scale, int base)
{
    if (op == TZ_STAY || !in_scale)
        return gen_tz_move(s, op);
    for (int t = 0; t < 3; t++) {
        q_tz n = gen_tz_move(s, 1 + (op - 1 + t) % 3);
        if (tz_in_scale(n, scale, base)) return n;
    }
    return s;
}

void gen_tz_start(const q_project *p, const q_tonnetz *t, q_tz *s, int *base)
{
    uint16_t sc = gen_scale(p);
    int r = gen_life_note(t->note, sc, p->tonic, 0), third = gen_life_note(t->note, sc, p->tonic, 2) - r, m;
    if (third == 3 || third == 4)       /* a diminished triad starts minor */
        m = third == 3;
    else {                              /* no third in the scale (a pentatonic): the quality with more notes in it */
        int maj = (sc >> (r + 4) % 12 & 1) + (sc >> (r + 7) % 12 & 1), min = (sc >> (r + 3) % 12 & 1) + (sc >> (r + 7) % 12 & 1);
        m = min > maj;
    }
    *s = (q_tz){0, 0, (uint8_t)m};
    *base = ((r - (m ? 4 : 0)) % 12 + 12) % 12;      /* the minor (0, 0)'s root is the lattice's 4 */
    if (t->scale && !tz_in_scale(*s, sc, *base))     /* IN SCALE and out: its first neighbour in the scale (P, L, R) */
        for (int op = TZ_P; op <= TZ_R; op++) {
            q_tz n = gen_tz_move(*s, op);
            if (tz_in_scale(n, sc, *base)) { *s = n; break; }
        }
}

/* the step tonnetz.n: a bar's chord from the walk, struck as CHORDS does */
static int tonnetz_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st)
{
    const q_tonnetz *t = &pt->tonnetz;
    q_tonnetz_run *r = &g->tonnetz;
    int fresh = !r->n;
    if (fresh)                          /* the source set mid-tour: its bars line up with the tour */
        r->n = (uint32_t)k;
    uint32_t n = r->n++;
    int step = (int)(n % 16), b = (int)(n / 16);
    if (fresh || step == 0) {
        int rr = t->ret ? (b - 1) % (t->len + 1) : (b - 1) % t->len;
        r->chaos_hit = 0;
        if (b == 0 || fresh || (t->ret && rr == t->len)) {   /* the start, or RETURN */
            int base;
            gen_tz_start(p, t, &r->cur, &base);
            r->base = (int8_t)base, r->applied = -1;
        } else {
            int op = TZ_OP(t, rr);
            q_tz written = gen_tz_step(r->cur, op, t->scale, gen_scale(p), r->base);
            if (t->chaos && (int)(gen_rand(rng) >> 8 & 127) < t->chaos) {   /* CHAOS: one of the other two (a stay: any) */
                int x = (int)(gen_rand(rng) >> 8);
                op = op == TZ_STAY ? 1 + x % 3 : 1 + (op - 1 + 1 + x % 2) % 3;
            }
            r->cur = gen_tz_step(r->cur, op, t->scale, gen_scale(p), r->base);
            r->chaos_hit = (uint8_t)(r->cur.i != written.i || r->cur.j != written.j || r->cur.m != written.m);   /* swapped, and it shows */
            if (r->cur.i > 9000 || r->cur.i < -9000)     /* far: back by 12 columns or 3 rows, the same pitch classes */
                r->cur.i = (int16_t)(r->cur.i % 12);
            if (r->cur.j > 9000 || r->cur.j < -9000)
                r->cur.j = (int16_t)(r->cur.j % 3);
            r->applied = (int8_t)rr;
        }
    }
    if (p->sound[track].model == M_KIT || step % (16 >> t->rate))
        return 0;
    uint8_t x[3], out[4];
    int poly = p->sound[track].poly ? p->sound[track].poly : 1, nn = poly < 3 ? poly : 3, v[4], i, j;
    gen_tz_pcs(r->cur, r->base, x);
    int root = t->note + (x[0] - t->note % 12 + 12) % 12;         /* ROOT: the root in the octave from NOTE */
    for (i = 0; i < nn; i++) v[i] = root + (x[i] - x[0] + 12) % 12;
    while (v[nn - 1 < 2 ? nn - 1 : 2] > 127 || root > 127)
        for (i = 0, root -= 12; i < nn; i++) v[i] -= 12;
    for (i = 1; i < nn; i++)
        for (j = i; j > 0 && v[j - 1] > v[j]; j--) { int y = v[j]; v[j] = v[j - 1]; v[j - 1] = y; }
    if (t->voice && r->nlast)
        voice_smooth(v, nn, r->last, r->nlast);
    for (i = 0; i < nn; i++) out[i] = (uint8_t)v[i];
    return strike_fill(st, out, nn, t->rate, t->strum, step, r->last, &r->nlast);
}

/* ORBITS */

/* the step orbits_n: each moon passing plays its note; two or more together, all accented */
static int orbits_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, q_step *st)
{
    const q_orbits *o = &pt->orbits;
    int hits = gen_orbits_hits(o, g->orbits_n++), nb = 0, kit = p->sound[track].model == M_KIT;
    uint16_t sc = gen_scale(p);
    for (int m = 0; m < o->moons; m++)
        if (hits >> m & 1)
            st->note[nb++] = (uint8_t)(kit ? KIT_NOTE + o->val[m] : gen_life_note(o->note, sc, p->tonic, o->val[m]));
    if (!nb)
        return 0;
    st->flags = SF_ON; st->len = 4; st->vel = 100; st->prob = 20;
    return nb > 1 ? (1 << nb) - 1 : 0;
}

int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st)
{
    __builtin_memset(st, 0, sizeof *st);
    if (pt->src == SRC_GRIDS) return grids_step(g, p, track, pt, k, rng, st);
    if (pt->src == SRC_LIFE) return life_step(g, p, track, pt, k, st);
    if (pt->src == SRC_LANES) return lanes_step(g, p, track, pt, st);
    if (pt->src == SRC_CHORDS) return chords_step(g, p, track, pt, k, st);
    if (pt->src == SRC_TONNETZ) return tonnetz_step(g, p, track, pt, k, rng, st);
    if (pt->src == SRC_ORBITS) return orbits_step(g, p, track, pt, st);
    return marbles_step(g, p, track, pt, rng, st);
}

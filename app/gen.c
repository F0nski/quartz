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

void gen_reset(q_gen *g, uint32_t *rng)
{
    __builtin_memset(g, 0, sizeof *g);
    for (int i = 0; i < 16; i++) { g->t.loop[i] = gen_float(rng); g->x.loop[i] = gen_float(rng); }
    g->drum_step = 8;          /* the first step draws a drum pattern */
}

int gen_loop(const q_pattern *pt)
{
    return pt->src == SRC_MARBLES ? pt->marbles.len : GRIDS_STEPS;
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

uint16_t gen_allowed(const q_project *p, const q_pattern *pt)
{
    uint16_t m = (uint16_t)((pt->marbles.notes[0] | pt->marbles.notes[1] << 8) & 0xFFF);
    if (m) return m;
    m = SCALE_MASK[p->mode];
    return (uint16_t)(((m << p->tonic) | (m >> (12 - p->tonic))) & 0xFFF);
}

int gen_quantize(int note, uint16_t allowed, int tonic, int steps)
{
    static const uint8_t STRENGTH[12] = { 0, 7, 4, 3, 10, 11, 2, 5, 9, 8, 1, 6 };
    int n = 0;
    for (int i = 0; i < 12; i++) n += allowed >> i & 1;
    int keep = n - steps * (n - 1) / 127, kept = 0;
    for (int i = 0; i < 12 && keep; i++) {
        int c = (tonic + STRENGTH[i]) % 12;
        if (allowed >> c & 1) { kept |= 1 << c; keep--; }
    }
    int best = 0, dist = 1 << 30;
    for (int c = 0; c < 128; c++) {       /* upward: a tie keeps the lower note */
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
        int c = (n + 1) * p / q - n * p / q;
        rat[i] = c > 4 ? 4 : c;
    }
}

/* One clock of TGenerator (t1 bit 0, t3 bit 1; t2 is the clock) and, when the step plays, one draw of
   OutputChannel::GenerateNewVoltage. */
static int marbles_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, uint32_t *rng, q_step *st)
{
    const q_marbles *m = &pt->marbles;
    float bias = m->bias / 127.0f, d = m->dejavu / 127.0f;
    int len = m->len;

    /* RandomSequence::NextVector: pulse widths (unused), u[0], u[1], p, jitter. 24 bits keep them below 1. */
    float x[6];
    uint32_t w = (uint32_t)(dejavu_next(&g->t, len, d, rng) * 4294967296.0f);
    for (int i = 0; i < 6; i++) { x[i] = (w >> 8) * (1.0f / 16777216.0f); w = w * 1664525u + 1013904223u; }
    const float *u = x + 2;

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
        float xu = dejavu_next(&g->x, len, d, rng);
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

int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st)
{
    __builtin_memset(st, 0, sizeof *st);
    if (pt->src == SRC_GRIDS) return grids_step(g, p, track, pt, k, rng, st);
    return marbles_step(g, p, track, pt, rng, st);
}

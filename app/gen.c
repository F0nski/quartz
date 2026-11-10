/* QUARTZ generators: the drum map of Grids and (to come) Marbles' random notes, one step at a time.
 *
 * Copyright (C) the QUARTZ authors. License: GPL-3.0-or-later.
 * The Grids part is a port of Mutable Instruments Grids (Emilie Gillet, GPL-3.0-or-later; the drum map is in
 * third_party/grids/map.h). Marbles is MIT-licensed. No libc headers: the app's C is built freestanding. */
#include "gen.h"
#include "third_party/grids/map.h"

#define GRIDS_STEPS 32

/* the same xorshift as the sequencer's (seq_rand); gen does not depend on seq.c */
static uint32_t gen_rand(uint32_t *rng)
{
    *rng ^= *rng << 13;
    *rng ^= *rng >> 17;
    *rng ^= *rng << 5;
    return *rng;
}

void gen_reset(q_gen *g, uint32_t *rng)
{
    (void)rng;
    __builtin_memset(g, 0, sizeof *g);
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

int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st)
{
    __builtin_memset(st, 0, sizeof *st);
    if (pt->src == SRC_GRIDS) return grids_step(g, p, track, pt, k, rng, st);
    return 0;      /* MARBLES: to come */
}

#ifndef QUARTZ_GEN_H
#define QUARTZ_GEN_H
#include "project.h"

/* Per track, the render's: what a generator pattern remembers from step to step. */
typedef struct { uint8_t perturb[3]; } q_gen;

void gen_reset(q_gen *g, uint32_t *rng);
/* The rank k of the pattern (0..loop - 1): fills *st (SF_ON when something plays, note[], vel, len 4, micro, ratchet)
   and returns the accented note slots (bit i: note[i]). rng: the sequencer's xorshift state. */
int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st);
int gen_loop(const q_pattern *pt);       /* GRIDS 32, MARBLES marbles.len; STEP: not called */

#endif

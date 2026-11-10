#ifndef QUARTZ_GEN_H
#define QUARTZ_GEN_H
#include "project.h"

/* Marbles' deja vu loop (RandomSequence): values 0..1 */
typedef struct { float loop[16]; int head, step; } q_dejavu;
/* Per track, the render's: what a generator pattern remembers from step to step. */
typedef struct {
    uint8_t perturb[3];                       /* GRIDS */
    q_dejavu t, x;                            /* MARBLES: the T and X sections' deja vu */
    uint8_t markov[16], div_pat[2][2];        /* div_pat: the divider pattern's ratios p/q, t1 then t3 */
    int markov_ptr, streak[2], drum_step, drum_index, div_left, div_n;
} q_gen;

extern const uint16_t SCALE_MASK[11];   /* bit n: n semitones above the tonic; CHROM..MIN PENTA, the order of ui.c MODE_NAME */
uint16_t gen_allowed(const q_project *p, const q_pattern *pt);  /* absolute pitch classes; notes 0: the scale from the tonic */
int gen_quantize(int note, uint16_t allowed, int tonic, int steps);  /* 0..127, in the notes STEPS keeps */

void gen_reset(q_gen *g, uint32_t *rng);   /* rng fills the deja vu loops */
/* The rank k of the pattern (0..loop - 1): fills *st (SF_ON when something plays, note[], vel, len 4, micro, ratchet)
   and returns the accented note slots (bit i: note[i]). rng: the sequencer's xorshift state. */
int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st);
int gen_loop(const q_pattern *pt);       /* GRIDS 32, MARBLES marbles.len; STEP: not called */

#endif

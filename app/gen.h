#ifndef QUARTZ_GEN_H
#define QUARTZ_GEN_H
#include "project.h"

/* Marbles' deja vu loop (RandomSequence): values 0..1 */
typedef struct { float loop[16]; int head, step; } q_dejavu;
#define LIFE_W 16
#define LIFE_H 8
/* LIFE's run, the render's (bit c of a row: column c; row 0 the lowest) */
typedef struct {
    uint16_t grid[LIFE_H], prev[2][LIFE_H], born[LIFE_H], mut[LIFE_H];   /* prev[0]: the generation before */
    uint32_t rng;                     /* its own xorshift, from SEED and DENS: the same pair, the same run */
    uint8_t seeded, seed, dens;       /* the pair the grid was drawn from */
    uint8_t tours, stuck;             /* tours since the last generation; stuck: dead or frozen, AUTO off */
    uint8_t gens, reseeds;            /* counters (wrap): the scene starts its animations on a change */
} q_life_run;
/* Per track, the render's: what a generator pattern remembers from step to step. */
typedef struct {
    uint8_t perturb[3];                       /* GRIDS */
    q_dejavu t, x;                            /* MARBLES: the T and X sections' deja vu */
    uint8_t markov[16], div_pat[2][2];        /* div_pat: the divider pattern's ratios p/q, t1 then t3 */
    int markov_ptr, streak[2], drum_step, drum_index, div_left, div_n;
    q_life_run life;                          /* LIFE */
} q_gen;

extern const uint16_t SCALE_MASK[11];   /* bit n: n semitones above the tonic; CHROM..MIN PENTA, the order of ui.c MODE_NAME */
uint16_t gen_scale(const q_project *p);   /* the project's scale: absolute pitch classes */
uint16_t gen_allowed(const q_project *p, const q_pattern *pt);  /* absolute pitch classes; notes 0: the scale from the tonic */
int gen_grids_level(const q_grids *g, int step, int voice);  /* a step's level 0..255 for the voice 0..2, without CHAOS */
uint16_t gen_kept(uint16_t allowed, int tonic, int steps);  /* the pitch classes STEPS keeps, the strongest from the tonic */
int gen_quantize(int note, uint16_t allowed, int tonic, int steps);  /* 1..127, in the notes STEPS keeps */

void gen_reset(q_gen *g, uint32_t *rng);   /* rng fills the deja vu loops */
/* The rank k of the pattern (0..loop - 1): fills *st (SF_ON when something plays, note[], vel, len 4, micro, ratchet)
   and returns the accented note slots (bit i: note[i]). rng: the sequencer's xorshift state. */
int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st);
int gen_loop(const q_pattern *pt);       /* GRIDS 32, MARBLES marbles.len, LIFE 16; STEP: not called */
/* the rows the column col plays, lowest first, at most POLY (3 on a KIT); BANDS: one row per band, its lowest living
   cell. Returns their number. */
/* the note row r plays on a synth: NOTE in the scale, r degrees of the scale up (at most 127) */
int gen_life_note(int note, uint16_t scale, int tonic, int r);
int gen_life_rows(const q_life *l, int kit, const uint16_t grid[LIFE_H], int col, uint8_t rows[4]);

#endif

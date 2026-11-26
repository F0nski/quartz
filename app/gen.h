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
/* CHORDS' run: steps since the reset (lined up on the tour); the last strike's notes (ascending) */
typedef struct { uint32_t n; uint8_t last[4], nlast; } q_chords_run;
/* a triad on the Tonnetz: node (i, j) is the pitch class 7i + 4j; m 0: major (i,j) (i+1,j) (i,j+1), 1: minor (i+1,j) (i,j+1) (i+1,j+1) */
typedef struct { int16_t i, j; uint8_t m; } q_tz;
/* TONNETZ's run: steps since the reset (lined up on the tour); the chord; base: the pitch class added to the lattice's;
   applied: the step of the last bar (-1 the start or a return); chaos_hit: that step swapped; the last strike */
typedef struct { uint32_t n; q_tz cur; int8_t base, applied; uint8_t chaos_hit, last[4], nlast; } q_tonnetz_run;
/* MIMIC's run: places are indices in the phrase, -1 none */
typedef struct {
    uint32_t n, rng, at;            /* steps since the reset; MIMIC's xorshift; at: the n of the last step that played */
    int8_t cur, prev, from;         /* the place played, the one before; from: the place left before last[0] */
    uint8_t wait, seed, seeded, listen;   /* wait: the steps left of cur's gap; seed: the SEED rng was drawn from */
    uint8_t last[4], nlast, jumps;  /* the last step's places; bit i: last[i] came from DRIFT */
} q_mimic_run;
/* Per track, the render's: what a generator pattern remembers from step to step. */
typedef struct {
    uint8_t perturb[3];                       /* GRIDS */
    q_dejavu t, x;                            /* MARBLES: the T and X sections' deja vu */
    uint8_t markov[16], div_pat[2][2];        /* div_pat: the divider pattern's ratios p/q, t1 then t3 */
    int markov_ptr, streak[2], drum_step, drum_index, div_left, div_n;
    q_life_run life;                          /* LIFE */
    uint32_t lanes_n;                         /* LANES: the steps played since the reset; lane l's head is lanes_n mod len[l] */
    q_chords_run chords;                      /* CHORDS */
    q_tonnetz_run tonnetz;                    /* TONNETZ */
    uint32_t orbits_n;                        /* ORBITS: the steps played since the reset */
    q_mimic_run mimic;                        /* MIMIC */
    uint32_t cipher_n;                        /* CIPHER: the step in the loop (back to 0 at LEN; FREE: on and on) */
} q_gen;

extern const uint16_t SCALE_MASK[11];   /* bit n: n semitones above the tonic; CHROM..MIN PENTA, the order of ui.c MODE_NAME */
uint16_t gen_scale(const q_project *p);   /* the project's scale: absolute pitch classes */
uint16_t gen_allowed(const q_project *p, const q_pattern *pt);  /* absolute pitch classes; notes 0: the scale from the tonic */
int gen_grids_level(const q_grids *g, int step, int voice);  /* a step's level 0..255 for the voice 0..2, without CHAOS */
uint16_t gen_kept(uint16_t allowed, int tonic, int steps);  /* the pitch classes STEPS keeps, the strongest from the tonic */
int gen_quantize(int note, uint16_t allowed, int tonic, int steps);  /* 1..127, in the notes STEPS keeps */
int gen_degrees(int note, uint16_t scale, int d);   /* note moved d notes of the scale up (down: d < 0), within 1..127 */
enum { KM_7 = 1, KM_SUS2 = 2, KM_SUS4 = 4, KM_ADD9 = 8, KM_INV = 16 };   /* KEYS SPLIT's modifiers: black keys 1-5 */
/* the chord of the scale's note root (scale: absolute pitch classes) with the modifiers mods: its notes low to high in
   out, those past 127 left out; returns their number (4 at most) */
int gen_keys_chord(uint16_t scale, int root, int mods, uint8_t out[4]);

void gen_reset(q_gen *g, uint32_t *rng);   /* rng fills the deja vu loops */
void gen_marbles_draw(q_gen *g, uint32_t *rng);   /* MARBLES: its deja vu loops drawn again (PRESET) */
/* The rank k of the pattern (0..loop - 1): fills *st (SF_ON when something plays, note[], vel, len 4, micro, ratchet)
   and returns the accented note slots (bit i: note[i]). rng: the sequencer's xorshift state. */
int gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st);
int gen_loop(const q_pattern *pt);       /* GRIDS 32, MARBLES marbles.len, the others 16; STEP: not called */
/* the rows the column col plays, lowest first, at most POLY (3 on a KIT); BANDS: one row per band, its lowest living
   cell. Returns their number. */
/* the note row r plays on a synth: NOTE in the scale, r degrees of the scale up (at most 127) */
int gen_life_note(int note, uint16_t scale, int tonic, int r);
/* a fresh run drawn from SEED and DENS, as the render starts it */
void gen_life_seed(q_life_run *l, int seed, int dens);
/* LANES: the degree 0..15 above note (in the scale) nearest to pitch; ties: the lower */
int gen_lanes_degree(int note, uint16_t scale, int tonic, int pitch);
/* CHORDS: chord slot's notes, ascending, at most poly: ROOT, or SMOOTH from prev (nprev 0: root position). Their number. */
int gen_chord_notes(const q_project *p, const q_chords *c, int slot, int poly, const uint8_t *prev, int nprev, uint8_t out[4]);
/* TONNETZ: P, L, R (TZ_STAY: s); the triad's pitch classes, root, third, fifth; a step under SCALE (IN SCALE: from op, P->L->R->P,
   the first that stays in scale, else s); the start triad at (0, 0) and its base */
q_tz gen_tz_move(q_tz s, int op);
void gen_tz_pcs(q_tz s, int base, uint8_t pcs[3]);
q_tz gen_tz_step(q_tz s, int op, int in_scale, uint16_t scale, int base);
void gen_tz_start(const q_project *p, const q_tonnetz *t, q_tz *s, int *base);
#define CIPHER_N 32
/* CIPHER's bank (spec 2026-11-19-quartz-cipher-design): a formula's name, its advised A, B, C, START and SPEED, and how
   many of A, B, C it uses (1: A, 2: A and B, 3: all; PARAM hides the others) */
typedef struct { const char *name; uint8_t a, b, c, start, speed, np; } q_cipher_def;
extern const q_cipher_def CIPHER_DEF[CIPHER_N];
uint8_t gen_cipher_byte(const q_cipher *c, uint32_t n);    /* the byte of step n: the formula of t = start x 256 + n x speed, & 255 */
void gen_cipher_preset(q_cipher *c, int formula);         /* formula and its advised A, B, C, START, SPEED; the rest kept */
int gen_mimic_gap(uint32_t ms, uint32_t step_ms, int lo);   /* MIMIC: ms in steps, rounded, lo..7 */
void gen_mimic_learn(q_mimic *m, int note, int gap);        /* the last event's gap := gap; note appended (gap 1); the oldest out past MIMIC_EV */
int gen_life_rows(const q_life *l, int kit, const uint16_t grid[LIFE_H], int col, uint8_t rows[4]);

#endif

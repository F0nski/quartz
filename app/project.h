#ifndef QUARTZ_PROJECT_H
#define QUARTZ_PROJECT_H
#include <stdint.h>

#define Q_TRACKS 4
#define Q_STEPS 64
#define Q_PATTERNS 8
#define Q_LOCKS 128
#define Q_SCENES 8
#define Q_SONG 32
#define Q_VERSION 18                /* q_project.version: the saved layout */
#define Q_PADS 16
#define M_KIT 24                   /* q_sound.model: the 16-pad kit */
#define KIT_NOTE 36                /* a KIT's note: KIT_NOTE + pad */
#define Q_SR 44117.647f            /* exactly 24 000 000 / 544 = 750000/17 Hz; the timing uses the ratio (seq.c) */

enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4T, DIV_4, DIV_2, DIV_1, DIV_N };

enum { SF_ON = 1, SF_SLIDE = 2, SF_ACCENT = 4 };          /* q_step.flags; bits 3-4: ratchet - 1; bits 5-7: STRUM */
#define SF_STRUM(s) ((s)->flags >> 5 & 7)                  /* a generator's step: its notes 1/24 step apart, lowest first */
#define STEP_RATCHET(s) ((((s)->flags >> 3) & 3) + 1)
enum { CD_NONE, CD_FIRST, CD_NFIRST, CD_PRE, CD_NPRE, CD_FILL, CD_NFILL };   /* q_step.cond < 16; else A:B: A = cond & 15, B = cond >> 4 */
enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE,
       LK_ATK, LK_DEC, LK_SUS, LK_REL, LK_FTYPE, LK_CUT, LK_RES, LK_ENVAMT,
       LK_SHAPE, LK_RATE, LK_DEPTH, LK_DEST, LK_AMODE, LK_AOCT, LK_ADIV, LK_AGATE,
       LK_I1, LK_I2, LK_I3, LK_IMIX, LK_DSEND, LK_RSEND, LK_PAN, LK_LEVEL,
       LK_SHAPE2, LK_RATE2, LK_DEPTH2, LK_DEST2, LK_N };   /* lockable q_sound fields (LK_N <= 64: audio.c's lk_mask) */
enum { LK_PUNCH_LO = LK_N, LK_PUNCH_HI };   /* a step's punch-ins, not q_sound fields: white keys 1-8 and 9-16, bit k = key k + 1 */
enum { IT_OFF, IT_DRIVE, IT_FOLD, IT_CRUSH, IT_CHORUS, IT_PHASER, IT_RING, IT_COMB, IT_N };   /* q_sound.itype: the insert */
enum { FT_LP, FT_HP, FT_BP };
enum { LS_SIN, LS_TRI, LS_SAW, LS_SQR, LS_SH };
enum { LD_HARM, LD_TIMB, LD_MORPH, LD_PITCH, LD_CUT, LD_RES, LD_LEVEL };
enum { AM_OFF, AM_UP, AM_DOWN, AM_UPDN, AM_RAND, AM_ORDER };
#define LRATE_SYNC 12              /* q_sound.lrate 0..11: synced to the tempo; 12..75: free */
/* note[1..3] 0 = none; vel 1..127; len 1..64 eighths of a step; prob 0..20 (x 5 %); micro -11..11 (24ths) */
typedef struct { uint8_t note[4], vel, len, prob, cond, flags; int8_t micro; } q_step;
typedef struct { uint8_t step, param, value; } q_lock;    /* value: the q_sound field's byte (tune as uint8_t) */
enum { PATH_FWD, PATH_REV, PATH_PINGPONG, PATH_SNAKE, PATH_COLUMNS, PATH_SPIRAL, PATH_RANDOM, PATH_XY, PATH_N };
/* XY: xlen, ylen 1..4, ydiv a DIV_* (the X clock is div) */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_LANES, SRC_CHORDS, SRC_TONNETZ, SRC_ORBITS, SRC_MIMIC, SRC_CIPHER, SRC_N };         /* q_pattern.src: where the notes come from */
/* 0..127 except voice 1..3, note 1..127 (0 is no note), pad 0..15 (a KIT: the pad each part plays), len 1..32 (the map's
   first len steps loop) */
typedef struct { uint8_t x, y, chaos, dens[3], voice, note, pad[3], len; } q_grids;
#define GRIDS_LEN 32                /* the drum map's steps: len's top and default */
/* 0..127 except tout 1..3, len 1..16, note 1..127, mode 0..6, range 1..4, gate 0..5 (1/8 to 4 steps: 1 << gate eighths);
 * notes: pitch classes allowed (bit n = C + n), 0 = the project's scale */
typedef struct { uint8_t tout, len, mode, bias, jitter, dejavu, spread, xbias, steps, note, range; uint8_t notes[2]; uint8_t gate; } q_marbles;
/* poly 1..4, note 1..127, map 0 PADS / 1 BANDS, seed and dens 0..127, autos 0..1, gen 0..3 (1, 2, 4, 8 tours), mut 0..127 */
typedef struct { uint8_t poly, note, map, seed, dens, autos, gen, mut; } q_life;
enum { L_TRIG, L_NOTE, L_OCT, L_DUR, L_VEL, L_N };   /* LANES' lanes */
/* note 1..127; nlen: lane l's length 1..16, less 1, in nibble l; trig: TRIG's cells (bit c); v: NOTE 0..15 (scale degrees; a
   KIT's pad), OCT 0..4 (-2..+2), DUR 0..4 (1/4..4 steps), VEL 0..3, a nibble a cell (cell c in byte c / 2, low first).
   Packed: 38 B a pattern, as the pool keeps its 8 KB spare (build.py) */
typedef struct { uint8_t note, nlen[3]; uint16_t trig; uint8_t v[L_N - 1][8]; } q_lanes;
static inline int lanes_len(const q_lanes *l, int i) { return (l->nlen[i >> 1] >> (i & 1) * 4 & 15) + 1; }
static inline int lanes_get(const q_lanes *l, int i, int c)
{
    return i == L_TRIG ? l->trig >> c & 1 : l->v[i - 1][c >> 1] >> (c & 1) * 4 & 15;
}
static inline void lanes_set_len(q_lanes *l, int i, int n)
{
    int s = (i & 1) * 4;
    l->nlen[i >> 1] = (uint8_t)((l->nlen[i >> 1] & ~(15 << s)) | (n - 1) << s);
}
static inline void lanes_set(q_lanes *l, int i, int c, int v)
{
    if (i == L_TRIG) {
        l->trig = (uint16_t)(v ? l->trig | 1u << c : l->trig & ~(1u << c));
        return;
    }
    int s = (c & 1) * 4;
    uint8_t *b = &l->v[i - 1][c >> 1];
    *b = (uint8_t)((*b & ~(15 << s)) | v << s);
}
enum { CS_TRIAD, CS_SEVENTH, CS_SUS2, CS_SUS4, CS_N };   /* a chord's size */
/* note 1..127, len 1..8, rate 0..4 (1, 2, 4, 8, 16 strikes a bar), strum 0..7, voice 0 ROOT / 1 SMOOTH; ch[i]: its degree
   0..6 in bits 0-2, its size in bits 3-4 */
typedef struct { uint8_t note, len, rate, strum, voice, ch[8]; } q_chords;
#define CH_DEG(c) ((c) & 7)
#define CH_SIZE(c) ((c) >> 3 & 3)
#define CH_MAKE(d, s) ((uint8_t)((d) | (s) << 3))
enum { TZ_STAY, TZ_P, TZ_L, TZ_R };                       /* TONNETZ's steps */
/* note 1..127, len 1..8, rate 0..4, strum 0..7, voice 0..1, chaos 0..127, scale 0 FREE / 1 IN SCALE, ret 0..1; seq: step i in
   bits 2i..2i+1 (TZ_*) */
typedef struct { uint8_t note, len, rate, strum, voice, chaos, scale, ret; uint16_t seq; } q_tonnetz;
/* note 1..127, moons 1..4; per 1..32, pha 0..31, val 0..15 (a scale degree; a KIT's pad - 1) */
typedef struct { uint8_t note, moons, per[4], pha[4], val[4]; } q_orbits;
/* bit m: moon m passes at step n (n mod its period = its phase mod its period); the run and the scene */
static inline int gen_orbits_hits(const q_orbits *o, uint32_t n)
{
    int hits = 0;
    for (int m = 0; m < o->moons; m++)
        if (n % o->per[m] == (uint32_t)(o->pha[m] % o->per[m])) hits |= 1 << m;
    return hits;
}
#define MIMIC_EV 32
/* n 0..MIMIC_EV, order 1..2, drift 0..127, loop 0 FREE / 1..16, seed 0..255; ev[i]: note 1..127, gap 0..7 steps to the next */
typedef struct { uint8_t n, order, drift, loop, seed; uint8_t ev[MIMIC_EV][2]; } q_mimic;
/* CIPHER: formula 0..CIPHER_N - 1, a b c 0..15, dens 0..127, len 0 FREE / 1..64, start 0..127 (t from start x 256),
 * speed 1..255 (t per step), note 1..127, range 1..4 octaves */
typedef struct { uint8_t formula, a, b, c, dens, len, start, speed, note, range; } q_cipher;
#define TZ_OP(t, i) ((t)->seq >> 2 * (i) & 3)
#define TZ_SET(t, i, o) ((t)->seq = (uint16_t)(((t)->seq & ~(3u << 2 * (i))) | (unsigned)(o) << 2 * (i)))
typedef struct { uint8_t len, div, nlocks, path, xlen, ylen, ydiv, src; q_grids grids; q_marbles marbles; q_life life; q_lanes lanes; q_chords chords; q_tonnetz tonnetz; q_orbits orbits; q_mimic mimic; q_cipher cipher; q_step steps[Q_STEPS]; q_lock locks[Q_LOCKS]; } q_pattern;
typedef struct { uint8_t used, mute, pat[Q_TRACKS]; } q_scene;
typedef struct { uint8_t scene, reps; } q_entry;          /* scene 0..7, reps 1..16 */
/* 0..127 except model 0..24, poly 1..4, tune -24..24, ftype 0..2, envamt and ldepth -64..63, lshape 0..4, ltrig 0 FREE 1 TRIG,
 * lrate 0..75, ldest 0..6, amode 0..5, aoct 1..4, adiv a DIV_*, agate 5..100, itype 0..7 (IT_*), pan -64..63;
 * i1..i3 the insert's knobs, imix its dry/wet, dsend and rsend the sends to the delay and the reverb; l2*: LFO 2, as l* */
typedef struct {
    uint8_t model, poly, glide, level; int8_t tune; uint8_t harm, timb, morph, decay;
    uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt;
    uint8_t lshape, ltrig, lrate; int8_t ldepth; uint8_t ldest, amode, aoct, adiv, agate;
    uint8_t itype, i1, i2, i3, imix, dsend, rsend; int8_t pan;
    uint8_t l2shape, l2trig, l2rate; int8_t l2depth; uint8_t l2dest;
} q_sound;
enum { PAD_BD, PAD_SN, PAD_HH };                          /* q_pad.engine */
/* tune -24..24; the rest 0..127; chr: 0 the engine's analog sound, 127 its synthetic one (Plaits' aux) */
typedef struct { uint8_t engine; int8_t tune; uint8_t harm, timb, decay, level, chr; } q_pad;
/* tempo in whole BPM, 40..240; dtime a DIV_*, dfb rsize rtone comp duck 0..127 (the delay and reverb returns, the master's compressor and duck) */
/* q_project.keys: where the white keys play the scale's notes, the black ones nothing */
enum { KEYS_OFF, KEYS_ALL };
typedef struct {
    uint8_t version; uint16_t tempo;
    uint8_t swing, tonic, mode, quant, click, keys, mute, song_on, nsong;   /* swing 50..75; tonic 0..11; mode 0..10;
                                                       quant 0 OFF 1 50 % 2 100 %; click bit 0 METRO, bit 1 COUNT;
                                                       keys a KEYS_*; mute bit per track */
    uint8_t dtime, dfb, rsize, rtone, comp, duck;
    uint8_t pattern[Q_TRACKS];                        /* the pattern each track is asked to play (and is edited) */
    q_sound sound[Q_TRACKS];
    q_pattern pat[Q_TRACKS][Q_PATTERNS];
    q_scene scene[Q_SCENES];
    q_entry song[Q_SONG];
    q_pad kit[Q_TRACKS][Q_PADS];
} q_project;

void project_defaults(q_project *p);
extern int store_project;              /* the project store_load and store_save use, 0..Q_PROJECTS - 1 */
int store_last(void);                 /* the project saved last (0 when none), to load at power-on */
int store_load(q_project *p);         /* 0 = loaded; else project_defaults and -1 */
int store_save(const q_project *p);   /* 0 = ok; -1 = refused while playing; else plat_store_save's code */
extern uint8_t chunk_buf[];           /* store.c's: one chunk, idle between a load and a save (ui.c's UNDO keeps its copy there) */

#endif

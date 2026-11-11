#ifndef QUARTZ_PROJECT_H
#define QUARTZ_PROJECT_H
#include <stdint.h>

#define Q_TRACKS 4
#define Q_STEPS 64
#define Q_PATTERNS 8
#define Q_LOCKS 128
#define Q_SCENES 8
#define Q_SONG 32
#define Q_VERSION 5                /* q_project.version: the saved layout */
#define Q_PADS 16
#define M_KIT 24                   /* q_sound.model: the 16-pad kit */
#define KIT_NOTE 36                /* a KIT's note: KIT_NOTE + pad */
#define Q_SR 44117.647f            /* exactly 24 000 000 / 544 = 750000/17 Hz; the timing uses the ratio (seq.c) */

enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4T, DIV_4, DIV_2, DIV_1, DIV_N };

enum { SF_ON = 1, SF_SLIDE = 2, SF_ACCENT = 4 };          /* q_step.flags; bits 3-4: ratchet - 1 */
#define STEP_RATCHET(s) ((((s)->flags >> 3) & 3) + 1)
enum { CD_NONE, CD_FIRST, CD_NFIRST, CD_PRE, CD_NPRE };   /* q_step.cond < 16; else A:B: A = cond & 15, B = cond >> 4 */
enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE,
       LK_ATK, LK_DEC, LK_SUS, LK_REL, LK_FTYPE, LK_CUT, LK_RES, LK_ENVAMT,
       LK_SHAPE, LK_RATE, LK_DEPTH, LK_DEST, LK_AMODE, LK_AOCT, LK_ADIV, LK_AGATE,
       LK_I1, LK_I2, LK_I3, LK_IMIX, LK_DSEND, LK_RSEND, LK_PAN, LK_LEVEL, LK_N };   /* lockable q_sound fields */
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
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_N };         /* q_pattern.src: where the notes come from */
/* 0..127 except voice 1..3, note 1..127 (0 is no note) */
typedef struct { uint8_t x, y, chaos, dens[3], voice, note; } q_grids;
/* 0..127 except tout 1..3, len 1..16, note 1..127, mode 0..6, range 1..4; notes: pitch classes allowed (bit n = C + n), 0 = the project's scale */
typedef struct { uint8_t tout, len, mode, bias, jitter, dejavu, spread, xbias, steps, note, range; uint8_t notes[2]; } q_marbles;
typedef struct { uint8_t len, div, nlocks, path, xlen, ylen, ydiv, src; q_grids grids; q_marbles marbles; q_step steps[Q_STEPS]; q_lock locks[Q_LOCKS]; } q_pattern;
typedef struct { uint8_t used, mute, pat[Q_TRACKS]; } q_scene;
typedef struct { uint8_t scene, reps; } q_entry;          /* scene 0..7, reps 1..16 */
/* 0..127 except model 0..24, poly 1..4, tune -24..24, ftype 0..2, envamt and ldepth -64..63, lshape 0..4, ltrig 0 FREE 1 TRIG,
 * lrate 0..75, ldest 0..6, amode 0..5, aoct 1..4, adiv a DIV_*, agate 5..100, itype 0..7 (IT_*), pan -64..63;
 * i1..i3 the insert's knobs, imix its dry/wet, dsend and rsend the sends to the delay and the reverb */
typedef struct {
    uint8_t model, poly, glide, level; int8_t tune; uint8_t harm, timb, morph, decay;
    uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt;
    uint8_t lshape, ltrig, lrate; int8_t ldepth; uint8_t ldest, amode, aoct, adiv, agate;
    uint8_t itype, i1, i2, i3, imix, dsend, rsend; int8_t pan;
} q_sound;
enum { PAD_BD, PAD_SN, PAD_HH };                          /* q_pad.engine */
typedef struct { uint8_t engine; int8_t tune; uint8_t harm, timb, decay, level; } q_pad;   /* tune -24..24; the rest 0..127 */
/* tempo in whole BPM, 40..240; dtime a DIV_*, dfb rsize rtone comp duck 0..127 (the delay and reverb returns, the master's compressor and duck) */
typedef struct {
    uint8_t version; uint16_t tempo;
    uint8_t swing, tonic, mode, quant, click, mute, song_on, nsong;   /* swing 50..75; tonic 0..11; mode 0..10;
                                                       quant 0 OFF 1 50 % 2 100 %; click bit 0 METRO, bit 1 COUNT;
                                                       mute bit per track */
    uint8_t dtime, dfb, rsize, rtone, comp, duck;
    uint8_t pattern[Q_TRACKS];                        /* the pattern each track is asked to play (and is edited) */
    q_sound sound[Q_TRACKS];
    q_pattern pat[Q_TRACKS][Q_PATTERNS];
    q_scene scene[Q_SCENES];
    q_entry song[Q_SONG];
    q_pad kit[Q_TRACKS][Q_PADS];
} q_project;

void project_defaults(q_project *p);
int store_load(q_project *p);         /* 0 = loaded; else project_defaults and -1 */
int store_save(const q_project *p);   /* 0 = ok; -1 = refused while playing; else plat_store_save's code */

#endif

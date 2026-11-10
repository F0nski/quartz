#ifndef QUARTZ_PROJECT_H
#define QUARTZ_PROJECT_H
#include <stdint.h>

#define Q_TRACKS 4
#define Q_STEPS 64
#define Q_PATTERNS 8
#define Q_LOCKS 128
#define Q_SCENES 8
#define Q_SONG 32
#define Q_VERSION 2                /* q_project.version: the saved layout */
#define Q_SR 44117.647f            /* exactly 24 000 000 / 544 = 750000/17 Hz; the timing uses the ratio (seq.c) */

enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4T, DIV_4, DIV_2, DIV_1, DIV_N };

enum { SF_ON = 1, SF_SLIDE = 2, SF_ACCENT = 4 };          /* q_step.flags; bits 3-4: ratchet - 1 */
#define STEP_RATCHET(s) ((((s)->flags >> 3) & 3) + 1)
enum { CD_NONE, CD_FIRST, CD_NFIRST, CD_PRE, CD_NPRE };   /* q_step.cond < 16; else A:B: A = cond & 15, B = cond >> 4 */
enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE, LK_N };   /* lockable q_sound fields */
/* note[1..3] 0 = none; vel 1..127; len 1..64 eighths of a step; prob 0..20 (x 5 %); micro -11..11 (24ths) */
typedef struct { uint8_t note[4], vel, len, prob, cond, flags; int8_t micro; } q_step;
typedef struct { uint8_t step, param, value; } q_lock;    /* value: the q_sound field's byte (tune as uint8_t) */
enum { PATH_FWD, PATH_REV, PATH_PINGPONG, PATH_SNAKE, PATH_COLUMNS, PATH_SPIRAL, PATH_RANDOM, PATH_XY, PATH_N };
/* XY: xlen, ylen 1..4, ydiv a DIV_* (the X clock is div) */
typedef struct { uint8_t len, div, nlocks, path, xlen, ylen, ydiv, pad; q_step steps[Q_STEPS]; q_lock locks[Q_LOCKS]; } q_pattern;
typedef struct { uint8_t used, mute, pat[Q_TRACKS]; } q_scene;
typedef struct { uint8_t scene, reps; } q_entry;          /* scene 0..7, reps 1..16 */
/* 0..127 except model 0..23, poly 1..4, tune -24..24 */
typedef struct { uint8_t model, poly, glide, level; int8_t tune; uint8_t harm, timb, morph, decay; } q_sound;
/* tempo in whole BPM, 40..240 */
typedef struct {
    uint8_t version; uint16_t tempo;
    uint8_t swing, tonic, mode, quant, click, mute, song_on, nsong;   /* swing 50..75; tonic 0..11; mode 0..10;
                                                       quant 0 OFF 1 50 % 2 100 %; click bit 0 METRO, bit 1 COUNT;
                                                       mute bit per track */
    uint8_t pattern[Q_TRACKS];                        /* the pattern each track is asked to play (and is edited) */
    q_sound sound[Q_TRACKS];
    q_pattern pat[Q_TRACKS][Q_PATTERNS];
    q_scene scene[Q_SCENES];
    q_entry song[Q_SONG];
} q_project;

void project_defaults(q_project *p);
int store_load(q_project *p);         /* 0 = loaded; else project_defaults and -1 */
int store_save(const q_project *p);   /* 0 = ok; -1 = refused while playing; else plat_store_save's code */

#endif

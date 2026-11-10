#ifndef QUARTZ_PROJECT_H
#define QUARTZ_PROJECT_H
#include <stdint.h>

#define Q_TRACKS 4
#define Q_STEPS 64
#define Q_SR 44117.647f            /* exactly 24 000 000 / 544 = 750000/17 Hz; the timing uses the ratio (seq.c) */

enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4T, DIV_4, DIV_2, DIV_1, DIV_N };

typedef struct { uint8_t on, note, vel, pad; } q_step;
typedef struct { uint8_t len, div; q_step steps[Q_STEPS]; } q_pattern;
/* 0..127 except model 0..23, poly 1..4, tune -24..24 */
typedef struct { uint8_t model, poly, glide, level; int8_t tune; uint8_t harm, timb, morph, decay; } q_sound;
/* tempo in whole BPM, 40..240 */
typedef struct { uint8_t version; uint16_t tempo; q_sound sound[Q_TRACKS]; q_pattern pat[Q_TRACKS]; } q_project;

void project_defaults(q_project *p);
int store_load(q_project *p);         /* 0 = loaded; else project_defaults and -1 */
int store_save(const q_project *p);   /* 0 = ok; -1 = refused while playing; else plat_store_save's code */

#endif

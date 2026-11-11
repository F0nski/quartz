/* The scenes of the sound pages (spec 9.3). Step 5a: the FM planet. */
#ifndef QUARTZ_SCENE_H
#define QUARTZ_SCENE_H
#include <stdint.h>

int  scene_has(int model);                        /* 1: the model has a scene (6-OP A/B/C, 2-OP FM) */
/* knobs: HARM, TIMB, MORPH, DECAY 0..127; hits: audio_hits of the track; ms: plat_ms() */
void scene_frame(const int16_t knobs[4], uint32_t hits, uint32_t ms);   /* builds the frame's primitives */
void scene_band(int32_t oy);                      /* the backdrop, then the primitives, into the band at oy */
void scene_enter(void);                           /* the scene appears (or shows another track): the notes before do not flash */

/* MARBLES (spec 5b): a marble for every note played, falling through the pins into its note's bin */
int  marbles_bin(uint16_t allowed, int note, int *nbins);   /* the bin of note's pitch class (nearest allowed one) */
void marbles_enter(void);                                   /* no marble in flight, history kept */
void marbles_drop(int note, uint32_t ms, uint32_t land_ms);  /* lands at land_ms, when its note sounds */
/* knobs: SPREAD, X BIAS, DEJA VU 0..127; allowed: gen_allowed; top_l "5/16", top_r "D DORIAN" */
void marbles_frame(const uint8_t knobs[3], uint16_t allowed, const char *top_l, const char *top_r, uint32_t ms);
void marbles_band(int32_t oy);
int  marbles_flying(void);                                  /* marbles in flight */
int  marbles_hist(int bin);                                 /* a bin's history 0..6 */

#endif

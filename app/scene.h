/* The scenes of the sound pages (spec 9.3). Step 5a: the FM planet. */
#ifndef QUARTZ_SCENE_H
#define QUARTZ_SCENE_H
#include <stdint.h>

int  scene_has(int model);                        /* 1: the model has a scene (6-OP A/B/C, 2-OP FM) */
/* knobs: HARM, TIMB, MORPH, DECAY 0..127; hits: audio_hits of the track; ms: plat_ms() */
void scene_frame(const int16_t knobs[4], uint32_t hits, uint32_t ms);   /* builds the frame's primitives */
void scene_band(int32_t oy);                      /* the backdrop, then the primitives, into the band at oy */

#endif

/* The scenes of the sound pages (spec 9.3): the FM planet (5a), KIT and single drums, MODAL, SPEECH (5c). */
#ifndef QUARTZ_SCENE_H
#define QUARTZ_SCENE_H
#include <stdint.h>

/* what a scene shows: the track's model; k: its ENGINE knobs HARM, TIMB, MORPH, DECAY 0..127 (a KIT: the edited pad's
   TUNE -24..24, HARM, TIMB, DECAY); hits: hits[0] audio_hits of the track (a KIT: audio_kit_hits per pad engine);
   pad_eng: a KIT's edited pad's engine, else -1 */
typedef struct { uint8_t model; int8_t pad_eng; int16_t k[4]; uint32_t hits[3]; } q_sview;
int  scene_has(int model);                        /* 1: the model has a scene (FM, SPEECH, MODAL, the drums, KIT) */
void scene_frame(const q_sview *v, uint32_t ms);  /* builds the frame's primitives; ms: plat_ms() */
void scene_band(int32_t oy);                      /* the backdrop, then the primitives, into the band at oy */
void scene_enter(void);                           /* the scene appears (or shows another track): the notes before do not flash */

/* MARBLES (spec 5b, 3): a fleet firing on the notes' crystals in a canyon; a shot lands when its note sounds */
typedef struct { uint8_t tout, mode, bias, jitter, dejavu, spread, xbias, len; int8_t pos; uint16_t allowed, kept;
                 uint32_t step_ms; } q_mview;
int  marbles_bin(uint16_t allowed, int note, int *nbins);   /* the crystal of note's pitch class (nearest allowed one) */
void marbles_enter(void);                                   /* no shot in flight, history kept */
void marbles_drop(int note, uint32_t ms, uint32_t land_ms);  /* lands at land_ms, when its note sounds */
void marbles_frame(const q_mview *v, uint32_t ms);
void marbles_band(int32_t oy);
int  marbles_flying(void);                                  /* shots in flight */
int  marbles_hist(int bin);                                 /* a crystal's history 0..6 */
int  marbles_ships(void);                                   /* ships drawn */

#endif

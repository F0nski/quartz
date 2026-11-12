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
/* step 5d1's scenes, each in its own file after scene.c (app_ui.c): scene_frame and scene_band call them */
void va_frame(const q_sview *v, uint32_t ms);     /* the crests: VA VCF, PHASE DIST, VA, WAVESHAPE (scene_va.c) */
void va_band(int32_t oy);
void wave_frame(const q_sview *v, uint32_t ms);   /* the bowl: WAVE TERRAIN, WAVETABLE (scene_wave.c) */
void wave_band(int32_t oy);
void garden_frame(const q_sview *v, uint32_t ms); /* the crystal garden: GRAIN FORM, ADDITIVE (scene_garden.c) */
void garden_band(int32_t oy);
void chip_frame(const q_sview *v, uint32_t ms);   /* the platforms: CHIPTUNE (scene_chip.c) */
void chip_band(int32_t oy);
void choir_frame(const q_sview *v, uint32_t ms);  /* the android choir: STRING MACH, CHORDS (scene_choir.c) */
void choir_band(int32_t oy);
void rope_frame(const q_sview *v, uint32_t ms);   /* the tightrope walker: STRING (scene_rope.c) */
void rope_band(int32_t oy);
void dance_frame(const q_sview *v, uint32_t ms);  /* the dancefloor: SWARM, NOISE, PARTICLE (scene_dance.c) */
void dance_band(int32_t oy);

/* HOME's tracks page (spec 5e): per track its model, the pattern playing, the one queued (0 none), muted, the step
   (-1 stopped), the loop's length, its note counter (audio_hits); the selected track */
typedef struct { uint8_t model[4], pat[4], next[4], mute[4]; int8_t pos[4], len[4]; uint32_t hits[4]; int8_t sel; } q_hview;
void home_frame(const q_hview *v, uint32_t ms);
void home_band(int32_t oy);

/* the sound pages ADSR, FILTER, LFO, ARP, INSERT, MIX (spec 5f): the page, the track's model, ui.c's insert type (0 INSERT
   OFF), the model has its own envelope, the track is a KIT; the page's 4 knobs in PAGE[]'s ranges; the note counter
   (audio_hits); the LFO's phase (audio_lfo_phase, -1 none); the arp's held notes and the one sounding (audio_arp, -1
   none); chord: the tonic's 1-3-5 when nothing is held */
enum { SP_ADSR, SP_FILTER, SP_LFO, SP_ARP, SP_INSERT, SP_MIX };
typedef struct {
    uint8_t page, model, itype, own_env, kit;
    int16_t k[4];
    uint32_t hits;
    float lfo_ph;
    uint8_t held[8], chord[3]; int8_t nheld, sounding;
} q_pview;
void sound_frame(const q_pview *v, uint32_t ms);
void sound_band(int32_t oy);

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

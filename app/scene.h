/* The scenes of the sound pages (spec 9.3): the FM planet (5a), KIT and single drums, MODAL, SPEECH (5c). */
#ifndef QUARTZ_SCENE_H
#define QUARTZ_SCENE_H
#include <stdint.h>
#include "project.h"
#include "gen.h"

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
typedef struct { uint8_t model[4], pat[4], next[4], mute[4], level[4]; int8_t pos[4], len[4]; uint32_t hits[4]; int8_t sel; } q_hview;
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

/* the global pages TEMPO, SCALE, KEYS, FX, SONG (spec 5g): the page, its 4 knobs in ui.c PAGE[]'s ranges; title: the scale ("A LOCRIAN";
   FX: the delay's division); notes: the 12 pitch classes' names from C; opt: the option rows' names (SCALE QUANT, CLICK;
   KEYS IN SCALE); tonic, mask (bit n: n semitones above it); inscale: KEYS' white keys play the scale; pos: the track's
   step, -1 stopped; held: SCALE the pitch classes held (bit n), KEYS the white keys (bit w); keyn: the MIDI note each white
   key plays, -1 none; hits: FX the note-ons of all tracks; song: SONG's entries, the cursor and the one playing (1..n) */
enum { GP_TEMPO, GP_SCALE, GP_KEYS, GP_FX, GP_SONG };
typedef struct {
    uint8_t page;
    int16_t k[4];
    const char *title;
    const char *const *notes;
    const char *const *opt[2];
    uint8_t nopt[2];
    uint8_t tonic, inscale;
    uint16_t mask;
    int8_t pos;
    uint16_t held;
    int8_t keyn[16];
    uint32_t hits;
    struct { int8_t n, cursor, playing, on; q_entry e[Q_SONG]; } song;
} q_glview;
void glo_frame(const q_glview *v, uint32_t ms);
void glo_band(const q_glview *v, int32_t oy);

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

/* LIFE's molecules (scene_life.c); ui.c fills the view */
typedef struct {
    q_life_run run;            /* audio_life's copy */
    int8_t col;                /* the column read, -1 stopped */
    uint8_t played;            /* bit r: row r plays in col (gen_life_rows) */
    uint8_t poly, autos, gen, mut, kit, map, seed;   /* the pattern's q_life, poly already capped on a KIT */
    char label[LIFE_H][5];     /* the rows' names, row 0 first */
    uint32_t step_ms;          /* a step in ms */
} q_lview;
void life_enter(void);                           /* arrival: the counters seen as they are, no animation */
void life_frame(const q_lview *v, uint32_t ms);  /* builds the primitives */
void life_band(const q_lview *v, int32_t oy);    /* the backdrop, p3d_draw, then the texts */

/* LANES' rows of blocks (scene_lanes.c); ui.c fills the view */
typedef struct {
    q_lanes lanes;             /* the pattern's */
    int8_t head[L_N];          /* each lane's cell playing, -1 none (stopped, or another pattern plays) */
    uint8_t sel, kit;          /* the lane edited; a KIT: no OCT row */
    int8_t flash;              /* the cell touched 0..15 on the edited lane, -1 none */
    uint32_t flash_ms;         /* when */
    char name[16][4];          /* NOTE cells' texts: note names, or "P1".."P16" on a KIT */
    uint32_t n, step_ms;       /* audio_lanes' count (the floor slides a notch when it changes), a step in ms */
} q_laneview;
void lanes_enter(void);                              /* arrival: the floor stands until the next step */
void lanes_frame(const q_laneview *v, uint32_t ms);  /* builds the primitives: the edited lane's blocks, the floor */
void lanes_band(const q_laneview *v, int32_t oy);    /* p3d_draw, then the overview and the texts */

/* CHORDS' heptagon (scene_chords.c); ui.c fills the view */
typedef struct {
    q_chords chords;           /* the pattern's */
    int8_t slot, held;         /* the chord playing (-1 none: stopped or another pattern plays), the chord held (-1 none) */
    uint8_t notes[4], n;       /* the notes shown on the keyboard: the last strike's, or the held chord's */
    uint8_t strikes;           /* q_chords_run.strikes: a change lights the strike */
    uint8_t step, kit;         /* the step in the bar 0..15; a KIT */
    uint8_t scale_pc[7];       /* the degrees' pitch classes from NOTE (I..VII) */
    char name[8];              /* the chord's name: "C", "Am", "G7", "Fsus4" (the held one's when held) */
    char banner[24];           /* "EDIT 3 - IV F" when held */
    uint32_t step_ms;          /* a step in ms (STRUM's delay: step_ms x strum / 24) */
} q_chview;
void chords_enter(void);                             /* arrival: no strike lights */
void chords_frame(const q_chview *v, uint32_t ms);   /* builds the primitives: the edges, the ghost, the chord */
void chords_band(const q_chview *v, int32_t oy);     /* p3d_draw, then the corners, squares, notches and keyboard */

/* TONNETZ's map (scene_tonnetz.c); ui.c fills the view */
typedef struct {
    q_tonnetz tz;              /* the pattern's */
    q_tz cur, trail[3];        /* the chord shown and the three before (trail[k].m 255: none) */
    int8_t base, applied, held;   /* the pitch-class base; the step of the last bar (-1 none); the step held (-1 none) */
    uint8_t chaos_hit, playing, kit, step;   /* the last step swapped by CHAOS; the pattern shown plays; a KIT; the step in the bar */
    uint8_t notes[4], n, strikes;            /* the last strike's notes (ascending); a change lights the strike */
    uint16_t scale;            /* the project's scale, absolute pitch classes */
    char name[8], banner[24];  /* the chord's name; "STEP 3 - R > DM" when held */
    uint32_t step_ms;          /* a step in ms (STRUM's delay: step_ms x strum / 24) */
} q_tzview;
void tonnetz_enter(void);                            /* arrival: no slide, no strike lights */
void tonnetz_frame(const q_tzview *v, uint32_t ms);  /* builds the primitives: edges, traces, the chord */
void tonnetz_band(const q_tzview *v, int32_t oy);    /* p3d_draw, then the nodes, names, steps and notches */

#endif

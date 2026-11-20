#ifndef QUARTZ_AUDIO_H
#define QUARTZ_AUDIO_H
#include <stdint.h>
#include "project.h"
#include "gen.h"
#include "seq.h"

/* The render (audio.c): sequencer -> voice pool -> Plaits voices, 0..2 on core 1, 3..5 on core 2.
 * Everything but app_render is called from the main loop; commands reach the render at its next block. */
/* fx_pct: qfx_block's part of core 1; since the start, steals: the voices taken from a sounding note (another track's,
 * origin's or a KIT engine's), cuts: those of them that cut another track's held note */
typedef struct { uint32_t core1_pct, core2_pct, xruns, active; int cpu2; uint32_t cpu2_stack, fx_pct, steals, cuts; } q_load;
typedef struct { uint8_t track, pat, step, note, vel, len; int8_t micro; } q_rec;   /* a note overdubbed: q_step's */

void audio_setup(q_project *p);                         /* app_init, before the audio starts (fm1-base has its own audio_init) */
void audio_play(int on);                                /* the sounding steps get their note-offs; on 1: from step 0 */
/* the keyboard, velocity 100; a note-off is never dropped; on 2: a note-on not overdubbed (written to a held step); on 3: 2, held by the arp after its key is released (latch) */
void audio_live_note(int track, int note, int on);
/* Overdub armed: the live notes while playing (count-in aside) are recorded, 8 held at most; the metronome per
 * prj->click (METRO), and a count-in on play (COUNT) */
void audio_rec(int on);
int  audio_take_rec(q_rec *r);                          /* 1 and a recorded note, 0 none; beyond 32 waiting, lost */
void audio_scene(int scene);                            /* while playing: at the next bar */
void audio_jump(int track, int pat);                    /* while playing: pat from the track's next step, at the same place */
void audio_print(int track, int pat);                   /* PRINT: the generator pattern's next pass, its steps to audio_take_print */
int  audio_take_print(q_print *out);                    /* 1 and a step PRINT sent, 0 none */
int  audio_scene_started(void);                         /* the scene started since the last call, -1 none */
int  audio_pattern(int track);                          /* the pattern playing */
int  audio_song_entry(void);                            /* -1 when not in song mode */
void audio_track_model(int track);                      /* after sound[track].model changed: its voices faded out
                                                         * over a block, then freed */
/* Each frame: builds one free voice's engine for a track's model (poly per track) while the render leaves it be, so
 * that a note-on finds its engine in place. The render switches at most one engine per block itself: a note-on
 * needing another starts a block later per extra switch, after its track's earlier note-ons */
void audio_prepare(void);
q_load audio_load(void);                                /* the cores' load over ~1 s, in % of the block's budget */
/* app_panic: silence from the next block on, until audio_unmute. fm1-base calls app_panic before an update (then
 * app_ui_init when it is refused, a reboot when committed) and before plat_restart (a reboot): app_ui_init unmutes */
void audio_mute(void);
void audio_unmute(void);
void audio_fill(int on);                                 /* FILL held: the steps with COND FILL play, !FILL not */
void audio_transpose(int d, int degrees);               /* the synth tracks' steps d semitones (degrees: of the scale) up */
void audio_punch(uint16_t mask);                        /* the punch-ins held, bit k = white key k + 1: from the next block on */
int audio_playing(void);
/* a LIFE pattern's run as the render left it (a copy read while it may write: one frame off at worst) */
void audio_life(int track, q_life_run *out);
uint32_t audio_lanes(int track);                        /* a LANES pattern's steps played since its reset (0: stopped or just reset) */
void audio_chords(int track, q_chords_run *out);        /* a CHORDS pattern's run as the render left it */
void audio_tonnetz(int track, q_tonnetz_run *out);      /* a TONNETZ pattern's run as the render left it */
void audio_marbles_draw(int track);                     /* MARBLES: the track's deja vu loops drawn again */
void audio_mimic(int track, q_mimic_run *out);           /* a MIMIC pattern's run as the render left it */
void audio_mimic_listen(int track, int on);              /* MIMIC: LISTEN on (the improvisation silent) or off */
uint32_t audio_cipher(int track);                       /* a CIPHER pattern's next step in its loop */
uint32_t audio_orbits(int track);                       /* an ORBITS pattern's steps played since its reset */
int audio_pos(int track);                               /* step in progress, -1 when stopped */
uint32_t audio_hits(int track);                         /* note-ons the track sounded since boot (compare by difference) */
uint32_t audio_kit_hits(int track, int engine);         /* a KIT's note-ons whose pad had that engine (PAD_BD, PAD_SN, PAD_HH) */
/* a MARBLES track's notes decided after the *seen-th (a step before they sound), oldest first, max at most, and the ms
   until each sounds; *seen moves on. More than 16 behind: the last 16 */
int audio_notes(int track, uint32_t *seen, uint8_t *out, uint32_t *in_ms, int max);

#endif

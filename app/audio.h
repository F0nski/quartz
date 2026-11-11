#ifndef QUARTZ_AUDIO_H
#define QUARTZ_AUDIO_H
#include <stdint.h>
#include "project.h"

/* The render (audio.c): sequencer -> voice pool -> Plaits voices, 0..2 on core 1, 3..5 on core 2.
 * Everything but app_render is called from the main loop; commands reach the render at its next block. */
typedef struct { uint32_t core1_pct, core2_pct, xruns, active; int cpu2; uint32_t cpu2_stack, fx_pct; } q_load;   /* fx_pct: qfx_block's part of core 1 */
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
void audio_punch(uint16_t mask);                        /* the punch-ins held, bit k = white key k + 1: from the next block on */
int audio_playing(void);
int audio_pos(int track);                               /* step in progress, -1 when stopped */
uint32_t audio_hits(int track);                         /* note-ons the track sounded since boot (compare by difference) */
/* the notes the track sounded after the *seen-th, oldest first, max at most; *seen moves on. More than 16 behind: the last 16 */
int audio_notes(int track, uint32_t *seen, uint8_t *out, int max);

#endif

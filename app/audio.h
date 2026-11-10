#ifndef QUARTZ_AUDIO_H
#define QUARTZ_AUDIO_H
#include <stdint.h>
#include "project.h"

/* The render (audio.c): sequencer -> voice pool -> Plaits voices, 0..2 on core 1, 3..5 on core 2.
 * Everything but app_render is called from the main loop; commands reach the render at its next block. */
typedef struct { uint32_t core1_pct, core2_pct, xruns, active; int cpu2; uint32_t cpu2_stack; } q_load;

void audio_setup(q_project *p);                         /* app_init, before the audio starts (fm1-base has its own audio_init) */
void audio_play(int on);                                /* the sounding steps get their note-offs; on 1: from step 0 */
void audio_live_note(int track, int note, int on);      /* the keyboard, velocity 100; a note-off is never dropped */
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
int audio_playing(void);
int audio_pos(int track);                               /* step in progress, -1 when stopped */

#endif

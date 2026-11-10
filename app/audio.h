#ifndef QUARTZ_AUDIO_H
#define QUARTZ_AUDIO_H
#include <stdint.h>
#include "project.h"

/* The render (audio.c): sequencer -> voice pool -> Plaits voices, 0..2 on core 1, 3..5 on core 2.
 * Everything but app_render is called from the main loop; commands reach the render at its next block. */
typedef struct { uint32_t core1_pct, core2_pct, xruns, active; int cpu2; uint32_t cpu2_stack; } q_load;

void audio_setup(q_project *p);                         /* app_init, before the audio starts (fm1-base has its own audio_init) */
void audio_play(int on);                                /* on 0: the sounding steps get their note-offs */
void audio_live_note(int track, int note, int on);      /* the keyboard, velocity 100 */
void audio_track_model(int track);                      /* after sound[track].model changed: its voices freed */
q_load audio_load(void);                                /* the cores' load over ~1 s, in % of the block's budget */
void audio_mute(void);                                  /* app_panic: silence from the next block on, for good */
int audio_playing(void);
int audio_pos(int track);                               /* step in progress, -1 when stopped */

#endif

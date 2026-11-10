#ifndef QUARTZ_VOICES_H
#define QUARTZ_VOICES_H
#include <stdint.h>

#define Q_VOICES 6                  /* slots 0..2 render on core 1, 3..5 on core 2 */

/* V_PREP: the main loop builds the slot's engine (audio_prepare); never allocated, never rendered */
enum { V_FREE, V_ON, V_RELEASED, V_PREP };

/* model: the engine the slot's voice holds, -1 unknown (voices_note_on sets the one the caller switches it to) */
typedef struct { uint8_t state; int8_t track; uint8_t note; int8_t model; uint32_t age; } q_vslot;
typedef struct { q_vslot v[Q_VOICES]; uint32_t clock; } q_voices;

void voices_init(q_voices *p);
/* slot 0..5: a free one holding the track's model first, then one holding a model no track wants (bit m of wanted:
 * a track is on model m). Only the track's slots on its model count toward poly: a fading old engine is not its own */
int  voices_pick(const q_voices *p, int track, int poly, int model, uint32_t wanted);  /* what voices_note_on takes */
int  voices_note_on(q_voices *p, int track, int note, int poly, int model, uint32_t wanted);
int  voices_note_off(q_voices *p, int track, int note);            /* released slot, -1 if none */
int  voices_retarget(q_voices *p, int track, int from, int to);    /* the track's V_ON slot holding from takes to, -1 none */
void voices_free(q_voices *p, int slot);                           /* the renderer reports silence */
void voices_release_track(q_voices *p, int track);                 /* all its slots -> V_FREE */

#endif

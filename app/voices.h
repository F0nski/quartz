#ifndef QUARTZ_VOICES_H
#define QUARTZ_VOICES_H
#include <stdint.h>

#define Q_VOICES 6                  /* slots 0..2 render on core 1, 3..5 on core 2 */

enum { V_FREE, V_ON, V_RELEASED };

typedef struct { uint8_t state; int8_t track; uint8_t note; uint32_t age; } q_vslot;
typedef struct { q_vslot v[Q_VOICES]; uint32_t clock; } q_voices;

void voices_init(q_voices *p);
int  voices_note_on(q_voices *p, int track, int note, int poly);   /* slot 0..5 */
int  voices_note_off(q_voices *p, int track, int note);            /* released slot, -1 if none */
void voices_free(q_voices *p, int slot);                           /* the renderer reports silence */
void voices_release_track(q_voices *p, int track);                 /* all its slots -> V_FREE */

#endif

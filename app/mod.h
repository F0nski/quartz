/* Per-voice ADSR and LFO, advanced once per Plaits block (12 frames). No platform dependency. */
#pragma once
#include <stdint.h>

/* a voice's settings, locks applied (audio.c) */
typedef struct { uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt; uint8_t lshape, ltrig, lrate;
                 int8_t ldepth; uint8_t ldest; } q_vparam;

#ifdef __cplusplus
extern "C" {
#endif
void mod_init(void);
void mod_set(int v, const q_vparam *p, int tempo);   /* core 1, each block before the render, and before a note-on */
void mod_note_on(int v, int accent);                 /* attack from the current level; TRIG: LFO phase 0 */
void mod_note_off(int v);                            /* release from the current level */
void mod_clock(int tempo, uint32_t n);               /* after each block: the FREE LFOs' clock */
void mod_restart(void);                              /* at PLAY: the FREE LFOs from 0 */
#ifdef __cplusplus
}
#endif

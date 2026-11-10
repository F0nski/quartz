/* The six Plaits voices (pq.cc), one engine each, behind a C interface.
 * Threading: every call for voice v (pq_set_engine, pq_set_patch, pq_note_on, pq_note_off) must be
 * serialized with pq_render(v), never run while the other core renders that voice. */
#pragma once

#define PQ_VOICES 6

#ifdef __cplusplus
extern "C" {
#endif
void pq_init(void);                                   /* builds the 6 voices, model 8 */
void pq_set_engine(int v, int model);                 /* 0..23 in Plaits' registration order; resets the voice */
void pq_set_patch(int v, float harm, float timb, float morph, float decay, float glide); /* 0..1 */
void pq_note_on(int v, float note, float vel);        /* MIDI note (tune included), vel 0..1 */
void pq_note_off(int v);
void pq_render(int v, float *out, unsigned n);        /* adds n mono samples, about +-1 */
int  pq_silent(int v);                                /* 1: released and under 1e-4 for 50 ms */
unsigned pq_voice_bytes(void);                        /* bytes per voice (object + buffer) */
#ifdef __cplusplus
}
#endif

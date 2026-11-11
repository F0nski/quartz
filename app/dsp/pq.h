/* The six Plaits voices (pq.cc), one engine each, behind a C interface.
 * Threading: every call for voice v (pq_set_engine, pq_set_patch, pq_note_on, pq_note_off, pq_glide_to) must be
 * serialized with pq_render(v), never run while the other core renders that voice. */
#pragma once

#define PQ_VOICES 6

/* a voice's modulations for its next Plaits block: env the ADSR 0..1; cutoff f / sample rate, clamped to
 * 0.0001..0.45; res the SVF's Q; pitch semitones; harm, timb, morph offsets; gain 0..1; type FT_* */
typedef struct { float env, cutoff, res, pitch, harm, timb, morph, gain; int type; } pq_mod;

#ifdef __cplusplus
extern "C" {
#endif
void pq_init(void);                                   /* builds the 6 voices, model 8 */
void pq_set_engine(int v, int model);                 /* 0..23 in Plaits' registration order; resets the voice */
void pq_set_patch(int v, float harm, float timb, float morph, float decay, float glide); /* 0..1 */
void pq_note_on(int v, float note, float vel);        /* MIDI note (tune included), vel 0..1 */
void pq_note_off(int v);
void pq_glide_to(int v, float note);                  /* new pitch, no trigger: slides over the voice's glide */
void pq_render(int v, float *out, unsigned n);        /* adds n mono samples in [-1, 1): 16-bit frames, never NaN */
int  pq_silent(int v);                                /* 1: released and under 1e-4 for 50 ms */
unsigned pq_voice_bytes(void);                        /* bytes per voice (object + buffer) */
void mod_voice_block(int v, pq_mod *m);               /* the app's (mod.c): voice v advanced by one Plaits block, 12 frames */
#ifdef __cplusplus
}
#endif

/* Per-voice ADSR and LFO, advanced once per Plaits block (12 frames); the per-track arpeggiator. No platform dependency. */
#pragma once
#include <stdint.h>
#include "seq.h"

/* a voice's settings, locks applied (audio.c) */
typedef struct { uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt; uint8_t lshape, ltrig, lrate;
                 int8_t ldepth; uint8_t ldest; } q_vparam;

#define ARP_HELD 8                  /* notes held at most; a 9th note-on is ignored */
#define ARP_IN 32                   /* note events per block; beyond, note-ons dropped, never a note-off */
#define ARP_OUT 48                  /* what arp_block may write */
enum { EV_LATCH = 4 };              /* a keyboard note-on with ARP held: kept after its key is released */
typedef struct { uint8_t mode, oct, div, gate; } q_arpp;
typedef struct { uint8_t note, vel, pat, step, flags, latched; } q_arpn;
typedef struct {
    q_arpn h[ARP_HELD]; int nh;     /* held notes, in arrival order */
    q_event in[ARP_IN]; int nin;    /* the events queued for this block */
    int mode;                       /* the mode last played */
    int run; uint32_t i, r, rd;     /* clock: next step at frame i + r / rd of the block (r in units of 1/rd; floor) */
    uint32_t idx, rng;              /* position in the pattern; RAND's state */
    int snd, track; uint8_t sn, sv, sp, ss; uint32_t soff;   /* the note sounding, its track and its off frame */
    int unlatch;
} q_arp;

#ifdef __cplusplus
extern "C" {
#endif
void mod_init(void);
void mod_set(int v, const q_vparam *p, int tempo);   /* core 1, each block before the render, and before a note-on */
void mod_note_on(int v, int accent);                 /* attack from the current level; TRIG: LFO phase 0 */
void mod_note_off(int v);                            /* release from the current level */
void mod_clock(int tempo, uint32_t n);               /* after each block: the FREE LFOs' clock */
void mod_restart(void);                              /* at PLAY: the FREE LFOs from 0 */
void arp_reset(q_arp *a);
void arp_input(q_arp *a, const q_event *e);          /* the track's event, offset in this block */
void arp_unlatch(q_arp *a);                          /* the latched notes released at the next arp_block */
/* the arp's events for the block's n frames, sorted by offset; consumes the queued inputs. One arp step lasts
   num / den frames (seq_step_ratio of the project's tempo and p->div, computed by the caller). */
int  arp_block(q_arp *a, const q_arpp *p, uint32_t num, uint32_t den, uint32_t n, q_event *out);
int  arp_sounding(const q_arp *a);                   /* notes it holds on the voices */
#ifdef __cplusplus
}
#endif

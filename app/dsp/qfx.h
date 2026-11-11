/* The track effects (qfx.cc): the four per-track inserts, mono, and the mix behind them (LEVEL, PAN, sends,
 * ping-pong delay, reverb, ducking, the punch-ins, compressor, limiter), behind a C interface.
 * Threading: qfx_insert(t, ...) must be serialized with every other call for track t and with qfx_init;
 * qfx_block with every other call. */
#pragma once

#include <stdint.h>

#define QFX_TRACKS 4
#define QFX_BLOCK 256   /* the most frames qfx_block takes */

/* a track's settings for the block, locks applied; duck 1: the track is ducked (not a KIT) */
typedef struct { uint8_t type, p1, p2, p3, mix, dsend, rsend, level, duck; int8_t pan; } qfx_track;
/* dly: the delay's time in frames (seq_step_ratio of tempo and dtime), halved by qfx to fit.
   punch: the punch-ins held, bit k = white key k + 1. fpb: frames per beat; bpos: frames since the bar's
   first beat at the block's start, 0 <= bpos < 4 * fpb (the grid of LOOP, GATE, AUTO-PAN, the STUTTERs) */
typedef struct { float dly; uint8_t dfb, rsize, rtone, comp, duck; uint16_t punch; float fpb, bpos; } qfx_master;

#ifdef __cplusplus
extern "C" {
#endif
void qfx_init(void);    /* every state cleared, the pool's buffers zeroed */
/* track t's insert on n mono samples in place; type IT_*, p1..p3 and mix 0..127. A type other than the
   track's last one clears its state first. IT_OFF or mix 0: buf untouched (and the state cleared) */
void qfx_insert(int t, int type, int p1, int p2, int p3, int mix, float *buf, unsigned n);
/* bus[t]: track t's voices, n mono samples (overwritten). duck_at: the frame of a KIT BD's note in the
   block, -1 none. l, r: the mix before MASTER, n samples each. n even, at most QFX_BLOCK. Settings that
   change since the last block are ramped over this one; the first block after qfx_init starts at its own. */
void qfx_block(float *const bus[QFX_TRACKS], const qfx_track tr[QFX_TRACKS], const qfx_master *m,
               int duck_at, float *l, float *r, unsigned n);
/* The reverb, apart so that the second core can run it: once per block, before qfx_block and never during it, it
   turns the sends qfx_block left into the returns the next qfx_block mixes: the reverb is one block late. */
void qfx_reverb(void);
float qfx_delay_fit(float frames);   /* frames halved until <= the buffer's length, in 44.1 kHz frames */
#ifdef __cplusplus
}
#endif

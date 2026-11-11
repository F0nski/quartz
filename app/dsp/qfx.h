/* The track effects (qfx.cc): the four per-track inserts, mono, behind a C interface.
 * Threading: qfx_insert(t, ...) must be serialized with every other call for track t and with qfx_init. */
#pragma once

#define QFX_TRACKS 4

#ifdef __cplusplus
extern "C" {
#endif
void qfx_init(void);    /* every state cleared, the pool's buffers zeroed */
/* track t's insert on n mono samples in place; type IT_*, p1..p3 and mix 0..127. A type other than the
   track's last one clears its state first. IT_OFF or mix 0: buf untouched (and the state cleared) */
void qfx_insert(int t, int type, int p1, int p2, int p3, int mix, float *buf, unsigned n);
#ifdef __cplusplus
}
#endif

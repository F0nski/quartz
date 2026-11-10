#ifndef QUARTZ_SEQ_H
#define QUARTZ_SEQ_H
#include "project.h"

typedef struct { uint32_t offset; uint8_t track, note, vel, on; } q_event;

/* Fields are private to seq.c. */
typedef struct {
    int running;
    uint64_t now;                   /* absolute frame of the next block */
    struct {
        int cur;                    /* step in progress, -1 before the first one */
        uint64_t next_i;            /* the next step starts at next_i + next_r / den frames: frame next_i + (next_r > 0) */
        uint32_t next_r;            /* 0 <= next_r < den */
        uint32_t num, den;          /* frames per step, num / den, for tempo and div below */
        int tempo, div;             /* tempo 0 = none yet */
        int playing;                /* a note-on is awaiting its note-off */
        uint8_t note;
        uint64_t off_at;
    } t[Q_TRACKS];
} q_seq;

/* frames per step as num / den, exact: the FM-1's rate is 750000/17 Hz */
void seq_step_ratio(int tempo, int div, uint32_t *num, uint32_t *den);
void seq_start(q_seq *s);                                   /* step 0 of every track at the next frame */
void seq_stop(q_seq *s, q_event *out, int *n);              /* note-offs of sounding notes; out holds Q_TRACKS */
/* Events of the next nframes, sorted by offset. Events past max are dropped, so size max for the worst case. */
int seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max);
int seq_pos(const q_seq *s, int track);                     /* step in progress, -1 when stopped */

#endif

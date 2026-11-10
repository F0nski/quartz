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
        uint64_t n;                 /* absolute number of the next step to start */
        uint64_t next_start;        /* absolute frame where step n starts */
        double anchor_f;            /* step k starts at ceil(anchor_f + (k - anchor_n) * spf) */
        uint64_t anchor_n;
        int tempo, div;             /* values the anchor was set for, tempo 0 = none yet */
        int playing;                /* a note-on is awaiting its note-off */
        uint8_t note;
        uint64_t off_at;
    } t[Q_TRACKS];
} q_seq;

double seq_step_frames(int tempo, int div);
void seq_start(q_seq *s);                                   /* step 0 of every track at the next frame */
void seq_stop(q_seq *s, q_event *out, int *n);              /* note-offs of sounding notes; out holds Q_TRACKS */
/* Events of the next nframes, sorted by offset. Events past max are dropped, so size max for the worst case. */
int seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max);
int seq_pos(const q_seq *s, int track);                     /* step in progress, -1 when stopped */

#endif

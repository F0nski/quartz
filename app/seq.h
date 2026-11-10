#ifndef QUARTZ_SEQ_H
#define QUARTZ_SEQ_H
#include "project.h"

#define Q_CLICK_TRACK Q_TRACKS          /* q_event.track of a metronome click: note 1 on a bar's first beat */
#define SEQ_QUEUE 64                    /* per track */
enum { EV_LEGATO = 1 };
typedef struct { uint32_t offset; uint8_t track, note, vel, on, pat, step, flags; } q_event;

/* Fields are private to seq.c. */
typedef struct {
    uint64_t next_i;            /* the next step starts at next_i + next_r / den frames: frame next_i + (next_r > 0) */
    uint32_t next_r;            /* 0 <= next_r <= den: = den only right after a re-anchor, < den after an advance */
    uint32_t num, den;          /* frames per step, num / den, for tempo and div below */
    int tempo, div;             /* tempo 0 = none yet */
} q_clock;
/* gate: a note-on's note-off, frames after; a tied note-on's note-off waits in the queue at the end of time */
typedef struct { uint64_t at; uint32_t gate; q_event e; } q_queued;
typedef struct { int n, ons; q_queued e[SEQ_QUEUE]; } q_queue;           /* by date, offs before ons at a date */
typedef struct { int step, pat, rank; q_clock c; } q_planned;            /* a step and its clock when planned */
typedef struct {
    q_clock x, y;               /* x: the start of the next step not begun; y: XY's row clock */
    int planned;                /* the step at x is planned */
    int k, row, pp;             /* rank in the pass; XY row; ping-pong phase */
    int pat, path;              /* taken at the wrap */
    int loops, pre, tie;        /* passes of the pattern; PRE's result; the last step planned tied a note */
    q_planned h[2];             /* the last two steps planned, [1] the newest */
} q_seq_track;
typedef struct {
    int running, click, beats, count;   /* beats since the start; count: beats of count-in */
    uint32_t rng;
    uint64_t now;                   /* absolute frame of the next block */
    q_clock beat;
    q_seq_track t[Q_TRACKS];
    q_queue q[Q_TRACKS + 1];        /* [Q_CLICK_TRACK]: the clicks */
} q_seq;

/* frames per step as num / den, exact: the FM-1's rate is 750000/17 Hz */
void seq_step_ratio(int tempo, int div, uint32_t *num, uint32_t *den);
void seq_start(q_seq *s, const q_project *p, int count_in);   /* count_in: the tracks start one bar later */
void seq_click(q_seq *s, int on);                             /* metronome events while on (and during a count-in) */
void seq_stop(q_seq *s, q_event *out, int *n);                /* out holds Q_TRACKS * SEQ_QUEUE */
/* Events of the next nframes, sorted by offset; what does not fit in max stays queued for the next block. */
int  seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max);
int  lock_find(const q_pattern *pt, int step, int param);              /* index, -1 none */
int  lock_set(q_pattern *pt, int step, int param, int value);          /* 0; -1 full (nothing written) */
void lock_clear_step(q_pattern *pt, int step);                         /* the main loop writes; the render reads */
int  seq_pos(const q_seq *s, int track);                      /* step whose grid span holds the block's start; -1 stopped or counting in */
int  seq_pattern(const q_seq *s, int track);                  /* the pattern playing */
int  seq_locate(const q_seq *s, const q_project *p, int track, int *micro);  /* nearest step to the block's start, micro -11..11; -1 if none */
int  seq_quantize(int micro, int quant);                      /* quant 0 OFF, 1 50 %, 2 100 %: micro scaled by 1, 1/2, 0, rounded toward 0 */

#endif

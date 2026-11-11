#ifndef QUARTZ_SEQ_H
#define QUARTZ_SEQ_H
#include "project.h"
#include "gen.h"

#define Q_CLICK_TRACK Q_TRACKS          /* q_event.track of a metronome click: note 1 on a bar's first beat */
#define SEQ_QUEUE 64                    /* per track */
enum { EV_LEGATO = 1, EV_ACCENT = 2, EV_REPEAT = 8 };    /* EV_REPEAT: a ratchet's hits after the first; 4: EV_LATCH */
/* from: an EV_LEGATO note-on's tied note, which it replaces; 0 otherwise */
typedef struct { uint32_t offset; uint8_t track, note, vel, on, pat, step, flags, from; } q_event;

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
    q_clock x;                  /* the start of the next step not begun */
    uint32_t pos;               /* that step's place since the anchor, in 96ths of a bar: XY's row */
    int planned;                /* the step at x is planned */
    int k, row, pp;             /* rank in the pass; XY row; ping-pong phase */
    int pat, path;              /* taken at the wrap */
    int loops, pre, tie;        /* passes of the pattern; PRE's result; the last step planned tied a note */
    int want, seen;             /* the pattern asked, taken at the wrap; p->pattern[t] last seen: a change asks */
    int anchor;                 /* to start over at the pending launch's bar */
    q_planned h[2];             /* the last two steps planned, [1] the newest */
} q_seq_track;
typedef struct {
    int running, click, beats, count;   /* beats since the start; count: beats of count-in */
    uint32_t rng;
    uint64_t now;                   /* absolute frame of the next block */
    int mute, seen_mute;            /* the mutes played; p->mute last seen: a change applies at once */
    int launch, lentry, bar;        /* scene pending, -1 none; its song entry, -1 by hand; its bar, -1 the next */
    int entry, started;             /* the song entry playing, -1 none; the scene launched + 1, 0 none */
    q_clock beat;
    q_seq_track t[Q_TRACKS];
    q_gen g[Q_TRACKS];              /* a generator pattern's state, per track */
    q_queue q[Q_TRACKS + 1];        /* [Q_CLICK_TRACK]: the clicks */
    /* a generator track's notes as they are decided (MARBLES a step ahead) with their frame: the last 16 per track, note n at
       n % 16, gcount[t] notes so far (the render writes, the main loop reads: seq_gen_notes) */
    uint8_t gnote[Q_TRACKS][16];
    uint32_t gat[Q_TRACKS][16], gcount[Q_TRACKS];
} q_seq;

/* frames per step as num / den, exact: the FM-1's rate is 750000/17 Hz */
void seq_step_ratio(int tempo, int div, uint32_t *num, uint32_t *den);
/* count_in: the tracks start one bar later; song mode (song_on, nsong > 0) launches entry 0 then */
void seq_start(q_seq *s, const q_project *p, int count_in);
void seq_click(q_seq *s, int on);                             /* metronome events while on (and during a count-in) */
void seq_launch(q_seq *s, int scene);                         /* at the next bar while running; by hand ends the song */
int  seq_scene_started(q_seq *s);                             /* the scene launched since the last call, -1 none */
int  seq_song_entry(const q_seq *s);                          /* the song entry playing, -1 when not in song mode */
void seq_stop(q_seq *s, q_event *out, int *n);                /* out holds Q_TRACKS * SEQ_QUEUE */
/* Events of the next nframes, sorted by offset; what does not fit in max stays queued for the next block. */
int  seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max);
/* a generator track's notes decided after the *seen-th, oldest first, max at most, with the frame each sounds at (low 32 bits);
   *seen moves on. More than 16 behind: the last 16 */
int  seq_gen_notes(const q_seq *s, int track, uint32_t *seen, uint8_t *notes, uint32_t *at, int max);
int  lock_find(const q_pattern *pt, int step, int param);              /* index, -1 none */
int  lock_set(q_pattern *pt, int step, int param, int value);          /* 0; -1 full (nothing written) */
void lock_clear_step(q_pattern *pt, int step);                         /* the main loop writes; the render reads */
float seq_bar_frames(const q_seq *s);                         /* frames since the bar's first beat at the block's start; -1 stopped or counting in */
int  seq_pos(const q_seq *s, int track);                      /* step whose grid span holds the block's start; -1 stopped or counting in */
int  seq_pattern(const q_seq *s, int track);                  /* the pattern playing */
/* the nearest step to the block's start, -1 if none; micro -11..11, pat its pattern (the next one's, past a wrap) */
int  seq_locate(const q_seq *s, const q_project *p, int track, int *micro, int *pat);
int  seq_quantize(int micro, int quant);                      /* quant 0 OFF, 1 50 %, 2 100 %: micro scaled by 1, 1/2, 0, rounded toward 0 */

#endif

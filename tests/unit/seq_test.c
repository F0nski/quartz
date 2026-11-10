#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "seq.c"

#define BLK 256

typedef struct { uint64_t at; q_event e; } ev;

static q_project P;
static q_seq S;
static ev EV[4096];
static int NEV;
static uint64_t T;

static void setup(void)
{
    project_defaults(&P);
    for (int i = 0; i < 16; i++) P.pat[0].steps[i].on = 1;
    memset(&S, 0, sizeof S);
    NEV = 0; T = 0;
    seq_start(&S);
}

static void run(int blocks)
{
    q_event out[64];
    while (blocks--) {
        int n = seq_block(&S, &P, BLK, out, 64);
        for (int i = 0; i < n; i++) {
            assert(out[i].offset < BLK);
            if (i) assert(out[i].offset >= out[i - 1].offset);
            EV[NEV].at = T + out[i].offset; EV[NEV++].e = out[i];
        }
        T += BLK;
    }
}

/* absolute frame of the k-th event of the given kind on a track */
static int64_t nth(int track, int on, int k)
{
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.track == track && EV[i].e.on == on && k-- == 0) return (int64_t)EV[i].at;
    return -1;
}

static void step_frames_values(void)
{
    assert(fabs(seq_step_frames(120, DIV_16) - 5514.705875) < 1e-3);
    assert(fabs(seq_step_frames(120, DIV_16T) - seq_step_frames(120, DIV_16) * 2 / 3) < 1e-6);
    assert(fabs(seq_step_frames(120, DIV_1) - 16 * seq_step_frames(120, DIV_16)) < 1e-6);
}

static void steps_land_on_exact_frames(void)
{
    setup();
    run(64);
    double spf = seq_step_frames(120, DIV_16);
    for (int k = 0; k < 3; k++) {
        assert(nth(0, 1, k) == (int64_t)ceil(k * spf));
        assert(nth(0, 0, k) == (int64_t)ceil((k + 0.5) * spf));
    }
    assert(nth(0, 1, 3) < 0);        /* step 3 starts after 64 blocks */
    assert(nth(1, 1, 0) < 0);
}

static void polyrhythm(void)
{
    setup();
    P.pat[1].len = 12; P.pat[1].div = DIV_8T;
    for (int i = 0; i < 12; i++) P.pat[1].steps[i].on = 1;
    run(400);
    int64_t loop = (int64_t)ceil(2 * (double)Q_SR);
    assert(nth(0, 1, 16) == loop);
    assert(nth(1, 1, 12) == loop);
}

static void length_shrink_wraps(void)
{
    setup();
    while (seq_pos(&S, 0) != 9) run(1);
    P.pat[0].len = 4;
    int64_t before = nth(0, 1, 10);
    assert(before < 0);
    while (seq_pos(&S, 0) == 9) run(1);
    assert(seq_pos(&S, 0) == 0);
}

static void tempo_change_keeps_grid(void)
{
    setup();
    while (seq_pos(&S, 0) != 3) run(1);
    P.tempo = 60;
    run(120);
    int64_t s3 = nth(0, 1, 3), s4 = nth(0, 1, 4), s5 = nth(0, 1, 5);
    double a = seq_step_frames(120, DIV_16), b = seq_step_frames(60, DIV_16);
    assert(llabs(s4 - (s3 + (int64_t)ceil(a))) <= 1);
    assert(llabs(s5 - (s4 + (int64_t)ceil(b))) <= 1);
}

static void stop_releases_notes(void)
{
    setup();
    run(1);                         /* note-on at 0, note-off at ~2757 */
    assert(NEV == 1 && EV[0].e.on);
    q_event out[Q_TRACKS]; int n = 0;
    seq_stop(&S, out, &n);
    assert(n == 1 && !out[0].on && out[0].track == 0 && out[0].note == 60);
    assert(seq_pos(&S, 0) == -1);
    n = 0; seq_stop(&S, out, &n);
    assert(n == 0);
}

static void events_sorted(void)
{
    setup();
    P.pat[1].div = DIV_32;
    for (int i = 0; i < 16; i++) P.pat[1].steps[i].on = 1;
    run(64);                        /* run() asserts offsets are < BLK and increasing */
    assert(NEV > 15);
}

int main(void)
{
    step_frames_values();
    steps_land_on_exact_frames();
    polyrhythm();
    length_shrink_wraps();
    tempo_change_keeps_grid();
    stop_releases_notes();
    events_sorted();
    puts("seq_test ok");
    return 0;
}

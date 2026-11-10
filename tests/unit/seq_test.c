#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
    for (int i = 0; i < 16; i++) P.pat[0][0].steps[i].flags = SF_ON;
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

/* frames per step as num / den; every expected frame below is ceil(x * num / den) in integers */
static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1) / b; }
static uint32_t N16, D16;

static void step_frames_values(void)
{
    uint32_t n, d;
    seq_step_ratio(120, DIV_16, &N16, &D16);
    assert(N16 == 45000000u && D16 == 8160u);         /* 750000/17 Hz x 60 / 120 BPM x 1/4 beat = 5514.7 */
    seq_step_ratio(120, DIV_16T, &n, &d);
    assert((uint64_t)n * D16 * 3 == (uint64_t)N16 * d * 2);
    seq_step_ratio(120, DIV_1, &n, &d);
    assert((uint64_t)n * D16 == (uint64_t)N16 * d * 16);
}

static void steps_land_on_exact_frames(void)
{
    setup();
    run(700);                       /* 2 full loops of the 16-step pattern */
    int k = 0;
    for (; nth(0, 1, k) >= 0 && nth(0, 0, k) >= 0; k++) {
        assert(nth(0, 1, k) == (int64_t)ceil_div((uint64_t)k * N16, D16));
        assert(nth(0, 0, k) == (int64_t)ceil_div((uint64_t)(2 * k + 1) * N16, 2 * D16));
    }
    assert(k >= 32);
    assert(nth(1, 1, 0) < 0);
}

static void polyrhythm(void)
{
    setup();
    P.pat[1][0].len = 12; P.pat[1][0].div = DIV_8T;
    for (int i = 0; i < 12; i++) P.pat[1][0].steps[i].flags = SF_ON;
    run(400);
    int64_t loop = (int64_t)ceil_div(2 * 750000, 17);   /* 2 s at 750000/17 Hz */
    assert(nth(0, 1, 16) == loop);
    assert(nth(1, 1, 12) == loop);
}

/* ceil(k * n1 / d1 + n2 / d2): step k starts at n1 / d1 frames each, the one after lasts n2 / d2 */
static int64_t ceil_then(uint32_t k, uint32_t n1, uint32_t d1, uint32_t n2, uint32_t d2)
{
    return (int64_t)ceil_div((uint64_t)k * n1 * d2 + (uint64_t)n2 * d1, (uint64_t)d1 * d2);
}

static void length_shrink_wraps(void)
{
    setup();
    while (seq_pos(&S, 0) != 9) run(1);
    assert(nth(0, 1, 9) >= 0 && nth(0, 1, 10) < 0);     /* steps 0..9 have started */
    P.pat[0][0].len = 4;
    while (seq_pos(&S, 0) == 9) run(1);
    assert(seq_pos(&S, 0) == 0);
    assert(nth(0, 1, 10) == (int64_t)ceil_div(10 * (uint64_t)N16, D16));   /* step 10 of the grid is the wrap to step 0 */
}

static void tempo_change_keeps_grid(void)
{
    static const int tempi[] = { 60, 77, 133, 200 };
    for (int pos = 1; pos < 14; pos++)
        for (unsigned c = 0; c < sizeof tempi / sizeof *tempi; c++) {
            setup();
            while (seq_pos(&S, 0) != pos) run(1);
            P.tempo = (uint16_t)tempi[c];
            run(160);
            int64_t s1 = nth(0, 1, pos + 1), s2 = nth(0, 1, pos + 2);
            uint32_t n, d;
            seq_step_ratio(tempi[c], DIV_16, &n, &d);
            assert(s1 == (int64_t)ceil_div((uint64_t)(pos + 1) * N16, D16));
            /* the fractional start of the next step is carried, so the one after is exact, not up to a frame later */
            assert(s2 == ceil_then(pos + 1, N16, D16, n, d));
        }
}

/* two tracks with the same step length stay on identical sample starts through several tempo changes */
static void tempo_changes_keep_tracks_aligned(void)
{
    static const int tempi[] = { 77, 133, 61, 240, 119, 40, 120 };
    setup();
    for (int i = 0; i < 16; i++) P.pat[1][0].steps[i].flags = SF_ON;
    for (unsigned c = 0; c < sizeof tempi / sizeof *tempi; c++) {
        run(37);
        P.tempo = (uint16_t)tempi[c];
    }
    run(600);
    int k = 0;
    for (; nth(0, 1, k) >= 0; k++) {
        assert(nth(0, 1, k) == nth(1, 1, k));
        assert(nth(0, 0, k) == nth(1, 0, k));
    }
    assert(k > 30);
}

/* the step in progress keeps its duration across a division change, the next one uses the new division */
static void division_change_mid_play(void)
{
    uint32_t n8, d8;
    setup();
    while (seq_pos(&S, 0) != 3) run(1);
    P.pat[0][0].div = DIV_8;
    run(120);
    seq_step_ratio(120, DIV_8, &n8, &d8);
    int64_t s4 = nth(0, 1, 4), s5 = nth(0, 1, 5);
    assert(s4 == (int64_t)ceil_div(4 * (uint64_t)N16, D16));        /* step 3 lasted a 16th */
    assert(s5 == ceil_then(4, N16, D16, n8, d8));                   /* step 4 lasts an 8th */
    assert(nth(0, 0, 4) == ceil_then(4, N16, D16, n8, 2 * d8));     /* and its note-off is at half of it */
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
    P.pat[1][0].div = DIV_32;
    for (int i = 0; i < 16; i++) P.pat[1][0].steps[i].flags = SF_ON;
    for (int i = 0; i < 16; i++) P.pat[2][0].steps[i].flags = SF_ON;     /* tracks 0 and 2 start together at every step */
    run(64);                        /* run() asserts offsets are < BLK and increasing */
    assert(NEV > 15);

    /* same-offset events keep the track order */
    int same = 0;
    for (int i = 1; i < NEV; i++)
        if (EV[i].at == EV[i - 1].at) {
            same++;
            assert(EV[i].e.track >= EV[i - 1].e.track);
        }
    assert(same > 0);
    /* per track: a note-off comes before the next note-on, never the reverse */
    for (int t = 0; t < Q_TRACKS; t++) {
        int sounding = 0;
        uint64_t last = 0;
        for (int i = 0; i < NEV; i++) {
            if (EV[i].e.track != t) continue;
            assert(EV[i].at >= last);
            last = EV[i].at;
            assert(EV[i].e.on != sounding);
            sounding = EV[i].e.on;
        }
    }
}

static void lock_set_find_clear(void)
{
    q_pattern *pt = &P.pat[0][0];
    project_defaults(&P);
    assert(lock_set(pt, 3, LK_HARM, 10) == 0);
    assert(lock_set(pt, 3, LK_TUNE, 200) == 0);
    assert(lock_set(pt, 7, LK_HARM, 20) == 0);
    assert(pt->nlocks == 3);
    assert(lock_find(pt, 3, LK_HARM) >= 0 && pt->locks[lock_find(pt, 3, LK_HARM)].value == 10);
    assert(pt->locks[lock_find(pt, 3, LK_TUNE)].value == 200);
    assert(pt->locks[lock_find(pt, 7, LK_HARM)].value == 20);
    assert(lock_find(pt, 7, LK_TIMB) == -1 && lock_find(pt, 4, LK_HARM) == -1);
    lock_clear_step(pt, 3);
    assert(pt->nlocks == 1);
    assert(lock_find(pt, 3, LK_HARM) == -1 && lock_find(pt, 3, LK_TUNE) == -1);
    assert(pt->locks[lock_find(pt, 7, LK_HARM)].value == 20);
    lock_clear_step(pt, 9);                                     /* nothing there */
    assert(pt->nlocks == 1);
}

static void lock_set_overwrites(void)
{
    q_pattern *pt = &P.pat[0][0];
    project_defaults(&P);
    lock_set(pt, 5, LK_MORPH, 1);
    assert(lock_set(pt, 5, LK_MORPH, 99) == 0);
    assert(pt->nlocks == 1 && pt->locks[lock_find(pt, 5, LK_MORPH)].value == 99);
}

static void lock_full_refused(void)
{
    q_pattern *pt = &P.pat[0][0];
    project_defaults(&P);
    for (int i = 0; i < Q_LOCKS; i++) assert(lock_set(pt, i / LK_N, i % LK_N, i) == 0);
    assert(pt->nlocks == Q_LOCKS);
    assert(lock_set(pt, 60, LK_HARM, 1) == -1);
    assert(pt->nlocks == Q_LOCKS);
    for (int i = 0; i < Q_LOCKS; i++) assert(pt->locks[lock_find(pt, i / LK_N, i % LK_N)].value == i);
    assert(lock_set(pt, 0, LK_HARM, 77) == 0);                  /* an overwrite still works when full */
}

int main(void)
{
    step_frames_values();
    steps_land_on_exact_frames();
    polyrhythm();
    length_shrink_wraps();
    tempo_change_keeps_grid();
    tempo_changes_keep_tracks_aligned();
    division_change_mid_play();
    stop_releases_notes();
    events_sorted();
    lock_set_find_clear();
    lock_set_overwrites();
    lock_full_refused();
    puts("seq_test ok");
    return 0;
}

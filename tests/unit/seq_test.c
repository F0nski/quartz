#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "seq.c"

#define BLK 256

typedef struct { uint64_t at; q_event e; } ev;

static q_project P;
static q_seq S;
static ev EV[16384];
static int NEV;
static uint64_t T;

static void setup(void)
{
    project_defaults(&P);
    for (int i = 0; i < Q_STEPS; i++) P.pat[0][0].steps[i].flags = SF_ON;
    memset(&S, 0, sizeof S);
    NEV = 0; T = 0;
    seq_start(&S, &P, 0);
}

/* one block of nframes, at most max events: returns how many came out */
static int block(uint32_t nframes, int max)
{
    q_event out[64];
    int n = seq_block(&S, &P, nframes, out, max);
    assert(n <= max);
    for (int i = 0; i < n; i++) {
        assert(out[i].offset < nframes);
        if (i) assert(out[i].offset >= out[i - 1].offset);
        EV[NEV].at = T + out[i].offset; EV[NEV++].e = out[i];
    }
    T += nframes;
    return n;
}

static void run(int blocks)
{
    while (blocks--) block(BLK, 64);
}

/* blocks until the next one starts at frame f */
static void run_to(uint64_t f)
{
    while (T < f) block(f - T < BLK ? (uint32_t)(f - T) : BLK, 64);
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
    q_event out[Q_TRACKS * SEQ_QUEUE]; int n = 0;
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

/* ceil(start of step k + spf x f / 4800) at 120 BPM in 1/16: the date of a step's event */
static int64_t at16(int k, int64_t f)
{
    int64_t x = (int64_t)k * N16 * 4800 + (int64_t)N16 * f, d = 4800 * (int64_t)D16;
    return x >= 0 ? (x + d - 1) / d : -(-x / d);
}

static void only_step(int t, int k)
{
    for (int i = 0; i < Q_STEPS; i++) P.pat[t][0].steps[i].flags = i == k ? SF_ON : 0;
}

/* the steps of the first n note-ons of a track */
static void played(int track, int n, int *steps)
{
    int k = 0;
    for (int i = 0; i < NEV && k < n; i++)
        if (EV[i].e.track == track && EV[i].e.on) steps[k++] = EV[i].e.step;
    assert(k == n);
}

static void expect_order(const int *want, int n)
{
    int got[64];
    played(0, n, got);
    for (int i = 0; i < n; i++) assert(got[i] == want[i]);
}

static void length_sets_gate(void)
{
    static const int lens[] = { 1, 4, 8, 64 };
    for (unsigned c = 0; c < sizeof lens / sizeof *lens; c++) {
        setup();
        only_step(0, 0);
        P.pat[0][0].steps[0].len = (uint8_t)lens[c];
        run(400);
        assert(nth(0, 1, 0) == 0);
        assert(nth(0, 0, 0) == at16(0, 600 * lens[c]));
        assert(nth(0, 1, 1) == at16(16, 0));                    /* nothing between: the next pass */
    }
}

static void micro_shifts_note(void)
{
    static const int m[] = { -11, 11 };
    for (int c = 0; c < 2; c++) {
        setup();
        P.pat[0][0].steps[4].micro = (int8_t)m[c];
        run(120);
        assert(nth(0, 1, 4) == at16(4, 200 * m[c]));          /* spf x 11 / 24 */
        assert(nth(0, 0, 4) == at16(4, 200 * m[c] + 2400));
    }
    setup();
    P.pat[0][0].steps[0].micro = -5;
    run(1);
    assert(nth(0, 1, 0) == 0);                                  /* before the start: the first frame */
}

static void swing_delays_odd_steps(void)
{
    setup();
    P.swing = 75;
    run(80);
    assert(nth(0, 1, 1) == at16(1, 2400));
    assert(nth(0, 1, 2) == at16(2, 0));
    assert(nth(0, 1, 3) == at16(3, 2400));
    setup();
    run(80);
    assert(nth(0, 1, 1) == at16(1, 0));
}

static void ratchet_repeats(void)
{
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= 3 << 3;                       /* ratchet 4 */
    run(40);
    for (int j = 0; j < 4; j++) {
        assert(nth(0, 1, j) == at16(0, 1200 * j));
        assert(nth(0, 0, j) == at16(0, 1200 * j + 600));
    }
    assert(nth(0, 1, 4) < 0);
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= 2 << 3;                       /* ratchet 3 */
    P.pat[0][0].steps[0].len = 16;
    run(40);
    for (int j = 0; j < 3; j++) {
        assert(nth(0, 1, j) == at16(0, 1600 * j));
        assert(nth(0, 0, j) == at16(0, 1600 * (j + 1)));     /* gates of spf / 3, len capped at a step */
    }
    assert(nth(0, 1, 3) < 0);
}

static void poly_step_plays_all_notes(void)
{
    static const uint8_t notes[4] = { 60, 64, 67, 0 };
    setup();
    only_step(0, 0);
    memcpy(P.pat[0][0].steps[0].note, notes, 4);
    run(40);
    assert(NEV == 6);
    for (int i = 0; i < 3; i++) {
        assert(EV[i].e.on && EV[i].at == 0 && EV[i].e.note == notes[i]);
        assert(!EV[3 + i].e.on && (int64_t)EV[3 + i].at == at16(0, 2400) && EV[3 + i].e.note == notes[i]);
    }
}

static void same_note_retrigger_not_cut(void)
{
    setup();
    for (int i = 0; i < Q_STEPS; i++) P.pat[0][0].steps[i].flags = i == 0 || i == 2 ? SF_ON : 0;
    P.pat[0][0].steps[0].len = 64;
    run(60);                                                    /* 2.8 steps past step 2 */
    int on2 = -1, offs = 0;
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.on && EV[i].e.step == 2) on2 = i;
    assert(on2 > 0 && (int64_t)EV[on2].at == at16(2, 0));
    assert(!EV[on2 - 1].e.on && EV[on2 - 1].e.note == 60 && EV[on2 - 1].at == EV[on2].at);
    for (int i = 0; i < NEV; i++)
        if (!EV[i].e.on) {
            offs++;
            assert(i == on2 - 1 || (int64_t)EV[i].at == at16(2, 2400));
        }
    assert(offs == 2);
}

static void tempo_change_with_queue(void)
{
    uint32_t n, d;
    setup();
    for (int i = 0; i < Q_STEPS; i++) P.pat[0][0].steps[i].micro = 11;
    run_to((uint64_t)at16(3, 3000));                            /* step 4 is in the queue */
    P.tempo = 60;
    run(200);
    int got[8];
    played(0, 8, got);
    for (int k = 0; k < 8; k++) {
        assert(got[k] == k);                                    /* each step once, in order */
        assert(k == 0 || nth(0, 1, k) > nth(0, 1, k - 1));
    }
    assert(nth(0, 1, 4) == at16(4, 2200));                      /* planned at 120 */
    seq_step_ratio(60, DIV_16, &n, &d);
    /* step 4 began after the change: it lasts a 1/16 at 60 */
    assert(nth(0, 1, 5) == (int64_t)ceil_div(4 * (uint64_t)N16 * d * 24 + 35 * (uint64_t)n * D16, 24 * (uint64_t)D16 * d));
}

static void pattern_switch_at_wrap(void)
{
    setup();
    for (int i = 0; i < Q_STEPS; i++) {
        P.pat[0][1].steps[i].flags = SF_ON;
        P.pat[0][1].steps[i].note[0] = 62;
    }
    while (seq_pos(&S, 0) != 5) run(1);
    P.pattern[0] = 1;
    while (seq_pos(&S, 0) != 0) {
        assert(seq_pattern(&S, 0) == 0);
        run(1);
    }
    assert(seq_pattern(&S, 0) == 1);
    run(30);
    for (int k = 6; k < 16; k++) {
        int i = 0, c = k;
        for (; i < NEV; i++) if (EV[i].e.on && c-- == 0) break;
        assert(EV[i].e.pat == 0 && EV[i].e.step == k && EV[i].e.note == 60);
    }
    int i = 0, c = 16;
    for (; i < NEV; i++) if (EV[i].e.on && c-- == 0) break;
    assert(EV[i].e.pat == 1 && EV[i].e.step == 0 && EV[i].e.note == 62);
}

static void path_orders(void)
{
    static const int snake[] = { 0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12, 0, 1 };
    static const int snake14[] = { 0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 0, 1 };
    static const int col32[] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15,
                                 16, 20, 24, 28, 17, 21, 25, 29, 18, 22, 26, 30, 19, 23, 27, 31, 0 };
    static const int rev5[] = { 4, 3, 2, 1, 0, 4, 3 };
    static const int pp4[] = { 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2 };
    static const int pp1[] = { 0, 0, 0 };
    struct { int path, len; const int *want; int n; } c[] = {
        { PATH_SNAKE, 16, snake, 18 }, { PATH_SNAKE, 14, snake14, 16 }, { PATH_COLUMNS, 32, col32, 33 },
        { PATH_REV, 5, rev5, 7 }, { PATH_PINGPONG, 4, pp4, 11 }, { PATH_PINGPONG, 1, pp1, 3 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        setup();
        P.pat[0][0].path = (uint8_t)c[i].path;
        P.pat[0][0].len = (uint8_t)c[i].len;
        seq_start(&S, &P, 0);                                   /* the path is taken at the start (and wraps) */
        run(800);
        expect_order(c[i].want, c[i].n);
    }
}

static void path_random_stays_in_length(void)
{
    int got[600] = { 0 }, count[64] = { 0 };
    setup();
    P.pat[0][0].path = PATH_RANDOM;
    P.pat[0][0].len = 6;
    seq_start(&S, &P, 0);
    while (nth(0, 1, 599) < 0) run(100);
    for (int i = 0; i < 600; i++) {
        int k = 0;
        for (int j = 0; j < NEV; j++) if (EV[j].e.on && k++ == i) { got[i] = EV[j].e.step; break; }
        assert(got[i] >= 0 && got[i] < 6);
        count[got[i]]++;
    }
    for (int i = 0; i < 6; i++) assert(count[i] >= 70 && count[i] <= 130);
}

static void path_change_at_wrap(void)
{
    int want[24];
    setup();
    while (seq_pos(&S, 0) != 5) run(1);
    P.pat[0][0].path = PATH_REV;
    run(400);
    for (int i = 0; i < 16; i++) { want[i] = i; }
    for (int i = 16; i < 24; i++) want[i] = 31 - i;
    expect_order(want, 24);
}

static void swing_follows_rank(void)
{
    static const int snake[] = { 0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12 };
    setup();
    P.pat[0][0].path = PATH_SNAKE;
    P.swing = 75;
    seq_start(&S, &P, 0);
    run(400);
    expect_order(snake, 16);
    for (int k = 0; k < 16; k++) assert(nth(0, 1, k) == at16(k, k & 1 ? 2400 : 0));
}

static void xy_order(int xlen, const int *want, int n)
{
    uint32_t nx, dx;
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->path = PATH_XY; pt->div = DIV_16T; pt->ydiv = DIV_4T;      /* 1/6 and 2/3 of a beat: 4 X steps a row */
    pt->xlen = (uint8_t)xlen; pt->ylen = 3;
    seq_start(&S, &P, 0);
    run(800);
    expect_order(want, n);
    seq_step_ratio(120, DIV_16T, &nx, &dx);
    for (int k = 0; k < n; k++) assert(nth(0, 1, k) == (int64_t)ceil_div((uint64_t)k * nx, dx));
}

static void xy_two_clocks(void)
{
    static const int x4[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1 };
    static const int x3[] = { 0, 1, 2, 0, 5, 6, 4, 5, 10, 8, 9, 10, 0, 1 };
    xy_order(4, x4, 14);
    xy_order(3, x3, 14);    /* Y moves a row every 4 X steps while X loops on 3, before the X step at its instant */
}

static void xy_y_is_silent(void)
{
    uint32_t n1, d1;
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->path = PATH_XY; pt->div = DIV_1; pt->ydiv = DIV_16;
    seq_start(&S, &P, 0);
    run(700);                                                   /* 2 bars and a bit: 32 Y steps */
    seq_step_ratio(120, DIV_1, &n1, &d1);
    assert(NEV == 5);                                           /* the X steps' ons and offs, nothing at Y's */
    for (int i = 0; i < NEV; i++)
        assert((int64_t)EV[i].at == (int64_t)ceil_div((uint64_t)i * n1, 2 * (uint64_t)d1));
}

static void xy_lengths_clamp(void)
{
    int got[20];
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->path = PATH_XY; pt->xlen = 1; pt->ylen = 1; pt->div = DIV_32;
    seq_start(&S, &P, 0);
    run(300);
    played(0, 20, got);
    for (int i = 0; i < 20; i++) assert(got[i] == 0);
}

static void overflow_stays_queued(void)
{
    static const uint8_t notes[4] = { 60, 64, 67, 71 };
    setup();
    for (int t = 0; t < Q_TRACKS; t++) {
        for (int i = 0; i < Q_STEPS; i++) {
            P.pat[t][0].steps[i].flags = SF_ON;
            memcpy(P.pat[t][0].steps[i].note, notes, 4);
        }
    }
    assert(block(BLK, 4) == 4);
    assert(block(BLK, 4) == 4 && EV[4].at == BLK);              /* the rest at the next block's start */
    for (int i = 0; i < 62; i++) block(BLK, 4);                 /* 16384 frames: steps 0..2 and their offs */
    int ons = 0, offs = 0;
    for (int i = 0; i < NEV; i++) EV[i].e.on ? ons++ : offs++;
    assert(ons == 48 && offs == 48);
    for (int t = 0; t < Q_TRACKS; t++)
        for (int k = 0; k < 4; k++) {
            int sounding = 0;
            for (int i = 0; i < NEV; i++) {
                if (EV[i].e.track != t || EV[i].e.note != notes[k]) continue;
                assert(EV[i].e.on != sounding);
                sounding = EV[i].e.on;
            }
        }
}

static void count_in_delays_tracks(void)
{
    uint32_t n4, d4, n1, d1;
    setup();
    seq_start(&S, &P, 1);
    seq_step_ratio(120, DIV_4, &n4, &d4);
    seq_step_ratio(120, DIV_1, &n1, &d1);
    run(1);
    assert(seq_pos(&S, 0) == -1);
    while (T + BLK <= ceil_div(n1, d1)) {
        assert(seq_pos(&S, 0) == -1);
        run(1);
    }
    run(200);
    for (int b = 0; b < 4; b++) {
        assert(nth(Q_CLICK_TRACK, 1, b) == (int64_t)ceil_div((uint64_t)b * n4, d4));
        int i = 0, c = b;
        for (; i < NEV; i++) if (EV[i].e.track == Q_CLICK_TRACK && c-- == 0) break;
        assert(EV[i].e.note == (b == 0));
    }
    assert(nth(Q_CLICK_TRACK, 1, 4) < 0);                      /* METRO off: the count-in only */
    assert(nth(0, 1, 0) == (int64_t)ceil_div(n1, d1));
    assert(seq_pos(&S, 0) >= 0);
}

static void click_follows_flag(void)
{
    uint32_t n4, d4;
    setup();
    seq_click(&S, 1);
    seq_step_ratio(120, DIV_4, &n4, &d4);
    run(800);                                                   /* 2.3 bars */
    for (int b = 0; b < 9; b++) {
        assert(nth(Q_CLICK_TRACK, 1, b) == (int64_t)ceil_div((uint64_t)b * n4, d4));
        int i = 0, c = b;
        for (; i < NEV; i++) if (EV[i].e.track == Q_CLICK_TRACK && c-- == 0) break;
        assert(EV[i].e.note == (b % 4 == 0));
    }
    assert(nth(Q_CLICK_TRACK, 0, 0) < 0);
    setup();
    seq_click(&S, 0);
    run(200);
    assert(nth(Q_CLICK_TRACK, 1, 0) < 0);
}

static void locate_and_quantize(void)
{
    int m;
    setup();
    run_to((uint64_t)at16(3, 1600));
    assert(seq_locate(&S, &P, 0, &m) == 3 && m == 8);
    run_to((uint64_t)at16(3, 3200));
    assert(seq_locate(&S, &P, 0, &m) == 4 && m == -8);
    assert(seq_quantize(8, 0) == 8 && seq_quantize(8, 1) == 4 && seq_quantize(-7, 1) == -3 && seq_quantize(8, 2) == 0);
    q_event out[Q_TRACKS * SEQ_QUEUE]; int n;
    seq_stop(&S, out, &n);
    assert(seq_locate(&S, &P, 0, &m) == -1);
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
    length_sets_gate();
    micro_shifts_note();
    swing_delays_odd_steps();
    ratchet_repeats();
    poly_step_plays_all_notes();
    same_note_retrigger_not_cut();
    tempo_change_with_queue();
    pattern_switch_at_wrap();
    path_orders();
    path_random_stays_in_length();
    path_change_at_wrap();
    swing_follows_rank();
    xy_two_clocks();
    xy_y_is_silent();
    xy_lengths_clamp();
    overflow_stays_queued();
    count_in_delays_tracks();
    click_follows_flag();
    locate_and_quantize();
    lock_set_find_clear();
    lock_set_overwrites();
    lock_full_refused();
    puts("seq_test ok");
    return 0;
}

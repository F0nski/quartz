#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gen.c"

/* gen_loop as seq.c calls it: the pattern flip_pt turns STEP right after its loop length is read (the UI's write
   landing between plan()'s reads) */
static q_pattern *flip_pt;
static int gen_loop_flip(const q_pattern *pt)
{
    int n = gen_loop(pt);
    if (pt == flip_pt) flip_pt->src = SRC_STEP;
    return n;
}
#define gen_loop gen_loop_flip
#include "seq.c"
#undef gen_loop

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

/* the rank is the steps since PLAY modulo LEN: shorter, the track lands where a STOP and a PLAY would have it */
static void length_shrink_wraps(void)
{
    setup();
    while (seq_pos(&S, 0) != 9) run(1);
    assert(nth(0, 1, 9) >= 0 && nth(0, 1, 10) < 0);     /* steps 0..9 have started */
    P.pat[0][0].len = 4;
    while (seq_pos(&S, 0) == 9) run(1);
    assert(seq_pos(&S, 0) == 10 % 4);
    assert(nth(0, 1, 10) == (int64_t)ceil_div(10 * (uint64_t)N16, D16));   /* on the grid */
}

/* a length changed and back: the track is on step 1 with a track that kept its length */
static void length_back_realigns(void)
{
    setup();
    for (int i = 0; i < 16; i++) P.pat[1][0].steps[i].flags = SF_ON;
    while (seq_pos(&S, 0) != 5) run(1);
    P.pat[0][0].len = 3;                                 /* a few odd passes */
    while (seq_pos(&S, 1) != 13) run(1);
    P.pat[0][0].len = 16;
    while (seq_pos(&S, 1) != 0) run(1);
    assert(seq_pos(&S, 0) == 0);
    while (seq_pos(&S, 1) != 7) run(1);
    assert(seq_pos(&S, 0) == 7);
}

/* 16 to 32: the pass goes on to step 17, the start stays the others' */
static void length_doubled_goes_on(void)
{
    setup();
    while (seq_pos(&S, 0) != 15) run(1);
    P.pat[0][0].len = 32;
    while (seq_pos(&S, 0) == 15) run(1);
    assert(seq_pos(&S, 0) == 16);
}

/* seq_jump: the pattern asked from the next step, at the steps since PLAY modulo its length */
static void jump_keeps_place(void)
{
    setup();
    for (int i = 0; i < Q_STEPS; i++) {
        P.pat[0][1].steps[i].flags = SF_ON;
        P.pat[0][1].steps[i].note[0] = 62;
    }
    P.pat[0][1].len = 8;
    while (seq_pos(&S, 0) != 5) run(1);
    P.pattern[0] = 1;
    seq_jump(&S, 0, 1);
    while (seq_pos(&S, 0) == 5) run(1);
    assert(seq_pattern(&S, 0) == 1 && seq_pos(&S, 0) == 6);
    while (seq_pos(&S, 0) != 7) run(1);
    while (seq_pos(&S, 0) == 7) run(1);
    assert(seq_pos(&S, 0) == 0);                         /* 8 steps since PLAY */
}

/* without a jump, a pattern of another length is taken at the pass's end, at the steps since PLAY modulo its length */
static void switch_lands_on_play_grid(void)
{
    setup();
    for (int i = 0; i < Q_STEPS; i++) P.pat[0][1].steps[i].flags = SF_ON;
    P.pat[0][1].len = 32;
    P.pat[0][0].len = 8;
    while (seq_pos(&S, 0) != 3) run(1);
    P.pattern[0] = 1;
    while (seq_pattern(&S, 0) != 1) run(1);
    assert(seq_pos(&S, 0) == 8);
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

static void repeat_flag_on_ratchet_repeats(void)
{
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= 3 << 3;                       /* ratchet 4 */
    run(40);
    int k = 0;
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.track == 0 && EV[i].e.on) assert(!(EV[i].e.flags & EV_REPEAT) == (k++ % 4 == 0));
    assert(k >= 4);
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

/* tempo changes rescale X's and Y's clocks each in its own units: a row starting with an X step still applies to it,
   from the first step on (row -1 would read steps[-4]) */
static void xy_tempo_change_keeps_rows(void)
{
    static const int tempos[] = { 97, 131, 240, 41, 173, 120 };
    int got[48];
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->path = PATH_XY; pt->div = DIV_16T; pt->ydiv = DIV_4T;      /* 4 X steps a row */
    pt->xlen = 4; pt->ylen = 3;
    seq_start(&S, &P, 0);
    P.tempo = 97;                                               /* before the first step is planned */
    for (int i = 0; i < 6; i++) {
        run(40);
        P.tempo = (uint16_t)tempos[i];
    }
    run(1500);
    played(0, 48, got);
    for (int i = 0; i < 48; i++) assert(got[i] == i % 12);
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
    int m, pat;
    setup();
    run_to((uint64_t)at16(3, 1600));
    assert(seq_locate(&S, &P, 0, &m, &pat) == 3 && m == 8 && pat == 0);
    run_to((uint64_t)at16(3, 3200));
    assert(seq_locate(&S, &P, 0, &m, &pat) == 4 && m == -8);
    assert(seq_quantize(8, 0) == 8 && seq_quantize(8, 1) == 4 && seq_quantize(-7, 1) == -3 && seq_quantize(8, 2) == 0);
    q_event out[Q_TRACKS * SEQ_QUEUE]; int n;
    seq_stop(&S, out, &n);
    assert(seq_locate(&S, &P, 0, &m, &pat) == -1);
}

/* the passes (loops) in which step k of a pattern of len 1/16 steps played, over n passes */
static int played_loops(int k, int len, int n, int *loops)
{
    int c = 0;
    run_to((uint64_t)at16(len * n, 0));
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.track == 0 && EV[i].e.on && EV[i].e.step == k) {
            int64_t l = 0;
            while (at16((int)(len * (l + 1)), 0) <= (int64_t)EV[i].at) l++;
            loops[c++] = (int)l;
        }
    return c;
}

static void cond_ab(void)
{
    int l[16];
    setup();
    P.pat[0][0].len = 4;
    only_step(0, 1);
    P.pat[0][0].steps[1].cond = 2 | 4 << 4;
    assert(played_loops(1, 4, 12, l) == 3 && l[0] == 1 && l[1] == 5 && l[2] == 9);
    static const uint8_t bad[] = { 0 | 4 << 4, 5 | 4 << 4, 1 | 1 << 4, 1 | 9 << 4, 9 };
    for (unsigned c = 0; c < sizeof bad; c++) {                 /* out of range: no condition */
        setup();
        P.pat[0][0].len = 4;
        only_step(0, 1);
        P.pat[0][0].steps[1].cond = bad[c];
        assert(played_loops(1, 4, 6, l) == 6);
    }
}

static void first_and_not_first(void)
{
    int l[16];
    setup();
    P.pat[0][0].len = 4;
    only_step(0, 2);
    P.pat[0][0].steps[2].cond = CD_FIRST;
    assert(played_loops(2, 4, 6, l) == 1 && l[0] == 0);
    setup();
    P.pat[0][0].len = 4;
    only_step(0, 2);
    P.pat[0][0].steps[2].cond = CD_NFIRST;
    assert(played_loops(2, 4, 6, l) == 5);
    for (int i = 0; i < 5; i++) assert(l[i] == i + 1);
}

static void pre_follows_last_condition(void)
{
    int l1[16], l2[16];
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->len = 3;
    pt->steps[0].cond = 1 | 2 << 4;
    pt->steps[1].cond = CD_PRE;
    pt->steps[2].cond = CD_NPRE;
    for (int k = 0; k < 3; k++) pt->steps[k].note[0] = (uint8_t)(60 + k);
    int n1 = played_loops(1, 3, 8, l1), n2 = played_loops(2, 3, 8, l2);
    assert(n1 == 4 && n2 == 4);
    for (int i = 0; i < 4; i++) assert(l1[i] == 2 * i && l2[i] == 2 * i + 1);
    setup();                                                    /* false at the start */
    only_step(0, 0);
    P.pat[0][0].steps[0].cond = CD_PRE;
    run(200);
    assert(nth(0, 1, 0) < 0);
}

static void probability_rates(void)
{
    static const int probs[] = { 10, 0, 20 };
    uint32_t n32, d32;
    seq_step_ratio(120, DIV_32, &n32, &d32);
    for (int c = 0; c < 3; c++) {
        setup();
        P.pat[0][0].len = 1;
        P.pat[0][0].div = DIV_32;
        P.pat[0][0].steps[0].prob = (uint8_t)probs[c];
        seq_start(&S, &P, 0);
        run_to(ceil_div(1000 * (uint64_t)n32, d32));            /* steps 0..999 */
        int ons = 0;
        for (int i = 0; i < NEV; i++) ons += EV[i].e.on;
        assert(c ? ons == (c == 2) * 1000 : ons >= 400 && ons <= 600);
    }
}

static void mute_silences_but_advances(void)
{
    setup();
    for (int i = 0; i < Q_STEPS; i++) P.pat[0][0].steps[i].note[0] = (uint8_t)(70 + i);
    P.pat[0][0].steps[0].len = 64;                              /* 8 steps */
    while (seq_pos(&S, 0) != 1) run(1);
    P.mute = 1;
    while (seq_pos(&S, 0) != 4) run(1);                        /* steps 2, 3, 4 planned muted */
    P.mute = 0;
    run(120);
    int got[4];
    played(0, 4, got);
    assert(got[0] == 0 && got[1] == 1 && got[2] == 5 && got[3] == 6);
    assert(nth(0, 1, 2) == at16(5, 0));
    int off = 0;
    for (int i = 0; i < NEV; i++)
        if (!EV[i].e.on && EV[i].e.note == 70) { off++; assert((int64_t)EV[i].at == at16(0, 38400)); }
    assert(off == 1);
}

static void accent_boosts_velocity(void)
{
    setup();
    P.pat[0][0].steps[0].flags |= SF_ACCENT;
    P.pat[0][0].steps[1].flags |= SF_ACCENT;
    P.pat[0][0].steps[1].vel = 60;
    P.pat[0][0].steps[2].vel = 60;
    run(60);
    int v[3] = { 0 }, k = 0;
    for (int i = 0; i < NEV && k < 3; i++) if (EV[i].e.on) v[k++] = EV[i].e.vel;
    assert(v[0] == 127 && v[1] == 92 && v[2] == 60);
}

static void accent_flag_on_accented_notes(void)
{
    setup();
    P.pat[0][0].steps[0].flags |= SF_ACCENT;
    P.pat[0][0].steps[0].note[1] = 64;
    run(60);
    int acc = 0, plain = 0;
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.on) {
            if (EV[i].e.step == 0) { assert(EV[i].e.flags & EV_ACCENT); acc++; }
            else if (EV[i].e.step == 2) { assert(!(EV[i].e.flags & EV_ACCENT)); plain++; }
        }
    assert(acc == 2 && plain >= 1);
}

static void slide_ties_to_next(void)
{
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= SF_SLIDE;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].note[0] = 64;
    run(40);                                                    /* steps 0..1 and the start of 2 */
    assert(NEV == 3);
    assert(EV[0].e.on && EV[0].e.note == 60 && EV[0].e.flags == 0 && EV[0].at == 0);
    assert(EV[1].e.on && EV[1].e.note == 64 && EV[1].e.flags == EV_LEGATO && (int64_t)EV[1].at == at16(1, 0));
    assert(EV[0].e.from == 0 && EV[1].e.from == 60);   /* the legato carries the tied note */
    assert(!EV[2].e.on && EV[2].e.note == 64 && (int64_t)EV[2].at == at16(1, 2400));
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= SF_SLIDE;
    run(40);
    assert(NEV == 2 && !EV[1].e.on && EV[1].e.note == 60 && (int64_t)EV[1].at == at16(1, 0));
    setup();                                                    /* a muted next step releases too */
    P.pat[0][0].steps[0].flags |= SF_SLIDE;
    while (seq_pos(&S, 0) != 0) run(1);
    P.mute = 1;
    run(40);
    assert(NEV == 2 && !EV[1].e.on && EV[1].e.note == 60 && (int64_t)EV[1].at == at16(1, 0));
    setup();                                                    /* a chain of slides: one off, at the end */
    for (int i = 0; i < 4; i++) {
        P.pat[0][0].steps[i].flags |= SF_SLIDE;
        P.pat[0][0].steps[i].note[0] = (uint8_t)(60 + i);
    }
    P.pat[0][0].steps[4].flags = 0;
    run(100);                                                   /* to just past step 4's start */
    assert(NEV == 5);
    for (int i = 0; i < 4; i++) assert(EV[i].e.on && EV[i].e.note == 60 + i && EV[i].e.flags == (i ? EV_LEGATO : 0) &&
                                         EV[i].e.from == (i ? 59 + i : 0));
    assert(!EV[4].e.on && EV[4].e.note == 63 && (int64_t)EV[4].at == at16(4, 0));
}

static void stop_during_tie_releases(void)
{
    setup();
    only_step(0, 0);
    P.pat[0][0].steps[0].flags |= SF_SLIDE;
    P.pat[0][0].steps[0].note[0] = 50;
    run(12);                                                    /* past step 0's gate, before step 1 */
    assert(NEV == 1);
    q_event out[Q_TRACKS * SEQ_QUEUE]; int n;
    seq_stop(&S, out, &n);
    assert(n == 1 && !out[0].on && out[0].note == 50 && out[0].step == 0);
}

static void events_carry_step(void)
{
    setup();
    for (int n = 0; n < 2; n++) {
        q_pattern *pt = &P.pat[0][n];
        pt->len = 4;
        for (int k = 0; k < 4; k++) {
            pt->steps[k].flags = SF_ON;
            pt->steps[k].note[0] = (uint8_t)(60 + 20 * n + k);
        }
    }
    P.pat[0][0].steps[3].flags |= SF_SLIDE;                     /* tied into pattern 1's step 0 */
    P.pat[0][1].steps[1].flags |= SF_SLIDE;                     /* released at step 2's start */
    P.pat[0][1].steps[2].flags = 0;
    while (seq_pos(&S, 0) != 2) run(1);
    P.pattern[0] = 1;
    run(300);
    int legato = 0, ons = 0, offs = 0;
    for (int i = 0; i < NEV; i++) {
        int n = EV[i].e.note >= 80;
        assert(EV[i].e.pat == n && EV[i].e.step == EV[i].e.note - 60 - 20 * n);
        if (EV[i].e.flags & EV_LEGATO) { legato++; assert(EV[i].e.pat == 1 && EV[i].e.step == 0); }
        EV[i].e.on ? ons++ : offs++;
    }
    assert(legato == 1 && ons > 10 && offs > 10);
    for (int i = 0; i < NEV; i++)                               /* the first pass of pattern 1 is ranks 4..7 */
        if (!EV[i].e.on && EV[i].e.note == 81) { assert((int64_t)EV[i].at == at16(6, 0)); break; }
}

/* every step of the pattern on, playing note */
static void fill(int t, int n, int note)
{
    for (int i = 0; i < Q_STEPS; i++) {
        P.pat[t][n].steps[i].flags = SF_ON;
        P.pat[t][n].steps[i].note[0] = (uint8_t)note;
    }
}

/* the first note-on of a track at or after frame f, -1 none */
static int first_on(int track, int64_t f)
{
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.track == track && EV[i].e.on && (int64_t)EV[i].at >= f) return i;
    return -1;
}

static void scene_launch_at_next_bar(void)
{
    static const uint8_t pat[Q_TRACKS] = { 1, 2, 3, 1 };
    int64_t b = at16(16, 0);                                    /* bar 1: ceil(2 x 750000/17) */
    setup();
    assert(b == (int64_t)ceil_div(2 * 750000, 17));
    for (int t = 0; t < Q_TRACKS; t++) {
        fill(t, 0, 60);
        fill(t, pat[t], 70 + t);
    }
    P.scene[2] = (q_scene){ 1, 8, { 1, 2, 3, 1 } };             /* track 3 muted */
    P.mute = 6;                                                 /* tracks 1 and 2 muted before */
    while (seq_pos(&S, 0) != 5) run(1);
    seq_launch(&S, 2);
    while (T + BLK <= (uint64_t)b) {
        run(1);
        assert(seq_scene_started(&S) == -1);
    }
    run(1);
    assert(seq_scene_started(&S) == 2 && seq_scene_started(&S) == -1);
    run(100);
    for (int t = 0; t < 3; t++) {
        int i = first_on(t, b);
        assert(i >= 0 && (int64_t)EV[i].at == b && EV[i].e.pat == pat[t] && EV[i].e.step == 0);
        assert(EV[i].e.note == 70 + t && seq_pattern(&S, t) == pat[t]);
    }
    assert(first_on(1, 0) == first_on(1, b) && first_on(2, 0) == first_on(2, b));     /* muted before */
    assert(first_on(3, 0) >= 0 && first_on(3, b) < 0);                                 /* muted from b */
    for (int i = 0; i < NEV; i++) assert(!EV[i].e.on || (int64_t)EV[i].at < b || EV[i].e.pat != 0);
}

/* a scene turning track 0 from its STEP pattern to a GRIDS one: from the bar, GRIDS from rank 0, as a fresh
   generator plays it (chaos 0: no draw) */
static void scene_launch_step_to_grids(void)
{
    int64_t b = at16(16, 0);
    q_gen g;
    uint32_t rng = 1;
    setup();
    q_pattern *gp = &P.pat[0][1];
    gp->src = SRC_GRIDS;
    gp->grids = (q_grids){ 32, 200, 0, { 100, 100, 100 }, 1, 67, { 0 }, GRIDS_LEN };
    P.scene[0] = (q_scene){ 1, 0, { 1, 0, 0, 0 } };
    while (seq_pos(&S, 0) != 5) run(1);
    seq_launch(&S, 0);
    run_to((uint64_t)at16(16 + 32, 0));
    gen_reset(&g, &rng);
    int i = first_on(0, b), ons = 0;
    assert(i >= 0 && (int64_t)EV[i].at == b && EV[i].e.pat == 1 && EV[i].e.step == 0);
    for (int k = 0; k < 32; k++) {
        q_step st;
        gen_step(&g, &P, 0, gp, k, &rng, &st);
        int n = 0;
        for (int j = 0; j < NEV; j++)
            if (EV[j].e.track == 0 && EV[j].e.on && (int64_t)EV[j].at == at16(16 + k, 0)) {
                assert(EV[j].e.pat == 1 && EV[j].e.step == k && EV[j].e.note == 67);
                n++;
            }
        assert(n == !!(st.flags & SF_ON));
        ons += n;
    }
    assert(ons > 4 && ons < 32);
}

static void scene_launch_keeps_offs(void)
{
    int64_t b = at16(16, 0);
    setup();
    for (int t = 0; t < 2; t++) {
        P.pat[t][0].len = 32;
        for (int i = 0; i < Q_STEPS; i++) P.pat[t][0].steps[i].flags = 0;
    }
    q_step *st = P.pat[0][0].steps;
    st[12] = (q_step){ { 50 }, 100, 64, 20, 0, SF_ON, 0 };      /* 8 steps: ends at step 20 */
    st[15] = (q_step){ { 51 }, 100, 1, 20, 0, SF_ON | 3 << 3, 11 };     /* ratchet 4: the last at 16 + 5/24 */
    st[16] = (q_step){ { 52 }, 100, 4, 20, 0, SF_ON, 0 };       /* planned before the launch */
    st = P.pat[1][0].steps;
    st[15] = (q_step){ { 40 }, 100, 4, 20, 0, SF_ON | SF_SLIDE, 0 };   /* tied into step 16, which goes */
    st[16] = (q_step){ { 41 }, 100, 4, 20, 0, SF_ON, 0 };
    P.pat[0][1].steps[0] = (q_step){ { 53 }, 100, 4, 20, 0, SF_ON, 0 };
    P.scene[1] = (q_scene){ 1, 0, { 1, 1, 0, 0 } };             /* track 1's pattern 1: no step on */
    run_to((uint64_t)at16(15, 3000));                           /* step 16 is planned */
    seq_launch(&S, 1);
    run_to((uint64_t)at16(24, 0));
    int ons[128] = { 0 }, offs[128] = { 0 };
    for (int i = 0; i < NEV; i++) EV[i].e.on ? ons[EV[i].e.note]++ : offs[EV[i].e.note]++;
    assert(ons[50] == 1 && offs[50] == 1);
    for (int i = 0; i < NEV; i++) if (!EV[i].e.on && EV[i].e.note == 50) assert((int64_t)EV[i].at == at16(12, 38400));
    assert(ons[51] == 3 && offs[51] == 3);
    for (int j = 0; j < 3; j++) assert(nth(0, 1, 1 + j) == at16(15, 2200 + 1200 * j));
    assert(ons[52] == 0 && offs[52] == 0 && ons[41] == 0);
    assert(ons[53] == 1 && nth(0, 1, 4) == b);
    assert(ons[40] == 1 && offs[40] == 1 && nth(1, 1, 0) == at16(15, 0) && nth(1, 0, 0) == b);
    assert(S.t[1].tie == 0 && S.q[0].n + S.q[1].n == 0 && S.q[0].ons + S.q[1].ons == 0);
}

/* track 0 plays pattern 1 from b on, and track 1 nothing */
static void scene_holds(int64_t b)
{
    for (int i = 0; i < NEV; i++) {
        if (!EV[i].e.on || (int64_t)EV[i].at < b) continue;
        assert(EV[i].e.track == 0 && EV[i].e.pat == 1);
    }
}

static void scene_then_project_copy_is_no_request(void)
{
    int64_t b = at16(16, 0);
    setup();
    fill(0, 1, 62);
    fill(1, 0, 40);
    P.scene[3] = (q_scene){ 1, 2, { 1, 0, 0, 0 } };
    while (seq_pos(&S, 0) != 5) run(1);
    seq_launch(&S, 3);
    run_to((uint64_t)at16(40, 0));                              /* past the scene pattern's wrap, not yet copied */
    assert(seq_scene_started(&S) == 3);
    scene_holds(b);
    P.pattern[0] = 1;                                           /* the main loop copies the scene */
    P.mute = 2;
    run_to((uint64_t)at16(72, 0));
    scene_holds(b);
    assert(first_on(0, at16(64, 0)) >= 0);
    P.mute = 0;                                                 /* a change still is a request */
    run_to((uint64_t)at16(80, 0));
    assert(first_on(1, at16(73, 0)) >= 0);
}

static void song_advances_and_loops(void)
{
    int seen[8], n = 0;
    setup();
    assert(seq_song_entry(&S) == -1);
    fill(0, 0, 60);
    fill(0, 1, 62);
    P.pat[0][1].len = 24;                                       /* 2 bars */
    P.scene[0] = (q_scene){ 1, 0, { 0, 0, 0, 0 } };
    P.scene[1] = (q_scene){ 1, 0, { 1, 0, 0, 0 } };
    P.song[0] = (q_entry){ 0, 2 };
    P.song[1] = (q_entry){ 1, 1 };
    P.nsong = 2;
    P.song_on = 1;
    seq_start(&S, &P, 0);
    run(1);
    assert(seq_scene_started(&S) == 0 && seq_song_entry(&S) == 0);
    seen[n++] = 0;
    while (T < (uint64_t)at16(88, 0)) {
        run(1);
        int e = seq_song_entry(&S), sc = seq_scene_started(&S);
        if (e != seen[n - 1]) {
            seen[n++] = e;
            assert(sc == P.song[e].scene);
            assert((int64_t)T - BLK <= at16(e ? 32 : 64, 0) && at16(e ? 32 : 64, 0) < (int64_t)T);
        } else {
            assert(sc == -1);
        }
    }
    assert(n == 3 && seen[1] == 1 && seen[2] == 0);
    static const int at[][3] = { { 0, 0, 0 }, { 16, 0, 0 }, { 32, 1, 0 }, { 56, 1, 0 }, { 63, 1, 7 }, { 64, 0, 0 } };
    for (unsigned c = 0; c < sizeof at / sizeof *at; c++) {
        int i = first_on(0, at16(at[c][0], 0));
        assert((int64_t)EV[i].at == at16(at[c][0], 0) && EV[i].e.pat == at[c][1] && EV[i].e.step == at[c][2]);
    }
}

static void song_with_count_in(void)
{
    int64_t b = at16(16, 0);
    setup();
    fill(0, 1, 62);
    P.scene[0] = (q_scene){ 1, 0, { 1, 0, 0, 0 } };
    P.song[0] = (q_entry){ 0, 1 };
    P.nsong = 1;
    P.song_on = 1;
    seq_start(&S, &P, 1);
    while (T + BLK <= (uint64_t)b) {
        run(1);
        assert(seq_song_entry(&S) == -1 && seq_scene_started(&S) == -1);
    }
    run(1);
    assert(seq_song_entry(&S) == 0 && seq_scene_started(&S) == 0);
    run_to((uint64_t)at16(40, 0));
    int i = first_on(0, 0);
    assert((int64_t)EV[i].at == b && EV[i].e.pat == 1 && EV[i].e.step == 0);
    i = first_on(0, at16(32, 0));                               /* one bar an entry: entry 0 again at bar 2 */
    assert((int64_t)EV[i].at == at16(32, 0) && EV[i].e.step == 0 && seq_song_entry(&S) == 0);
}

/* random attributes and changes: every note-on gets one note-off (or a legato that replaces it), in order */
static uint32_t R = 12345;
static uint32_t rnd(void) { R ^= R << 13; R ^= R >> 17; R ^= R << 5; return R; }

static void every_on_gets_its_off(void)
{
    static int c[Q_TRACKS][128];
    int legatos[Q_TRACKS] = { 0 }, ons = 0, ties = 0;
    q_event out[Q_TRACKS * SEQ_QUEUE];
    project_defaults(&P);
    memset(c, 0, sizeof c);
    for (int t = 0; t < Q_TRACKS; t++)
        for (int n = 0; n < Q_PATTERNS; n++) {
            q_pattern *pt = &P.pat[t][n];
            pt->len = (uint8_t)(1 + rnd() % 64);
            pt->div = (uint8_t)(rnd() % 5);                     /* 1/32 to 1/8: enough steps in the run */
            pt->src = (uint8_t)(rnd() % 2 ? SRC_STEP : rnd() % SRC_N);
            pt->grids.chaos = (uint8_t)(rnd() % 128);
            for (int i = 0; i < 3; i++) pt->grids.dens[i] = (uint8_t)(rnd() % 128);
            pt->marbles.len = (uint8_t)(1 + rnd() % 16);
            pt->marbles.tout = (uint8_t)(1 + rnd() % 3);
            for (int k = 0; k < Q_STEPS; k++) {
                q_step *st = &pt->steps[k];
                for (int i = 0; i < 4; i++) st->note[i] = (uint8_t)(rnd() % 3 ? 60 + rnd() % 6 : 0);
                st->vel = (uint8_t)(1 + rnd() % 127);
                st->len = (uint8_t)(1 + rnd() % 64);
                st->prob = (uint8_t)(rnd() % 2 ? 20 : rnd() % 22);
                st->cond = (uint8_t)(rnd() % 2 ? CD_NONE : rnd());
                st->flags = (uint8_t)(rnd() % 32 | (rnd() % 4 ? SF_ON : 0));
                st->micro = (int8_t)((int)(rnd() % 23) - 11);
            }
        }
    for (int i = 0; i < Q_SCENES; i++) {                        /* one in four unused */
        P.scene[i] = (q_scene){ (uint8_t)(rnd() % 4 != 0), (uint8_t)(rnd() % 16), { 0 } };
        for (int t = 0; t < Q_TRACKS; t++) P.scene[i].pat[t] = (uint8_t)(rnd() % Q_PATTERNS);
    }
    for (int i = 0; i < Q_SONG; i++) P.song[i] = (q_entry){ (uint8_t)(rnd() % Q_SCENES), 1 };
    P.nsong = 3;
    P.song_on = 1;
    int launches = 0, started = 0, entry = -1, entries = 0, srcs = 0, gens = 0;
    memset(&S, 0, sizeof S);
    seq_start(&S, &P, 0);
    for (int b = 0; b < 20000; b++) {
        uint32_t r = rnd() % 64;
        int t = (int)(rnd() % Q_TRACKS);
        q_pattern *pt = &P.pat[t][rnd() % Q_PATTERNS];
        if (r == 0) P.pattern[t] = (uint8_t)(rnd() % Q_PATTERNS);
        if (r == 1) pt->len = (uint8_t)(1 + rnd() % 64);
        if (r == 2) P.tempo = (uint16_t)(40 + rnd() % 201);
        if (r == 3) P.mute = (uint8_t)(rnd() % 16);
        if (r == 5) P.swing = (uint8_t)(50 + rnd() % 26);
        if (r == 6) { seq_launch(&S, (int)(rnd() % Q_SCENES)); launches++; }
        if (r == 7) { pt->src = (uint8_t)(rnd() % SRC_N); srcs++; }
        if (r == 9) pt->marbles.len = (uint8_t)(1 + rnd() % 16);
        if (r == 10) { pt->grids.dens[rnd() % 3] = (uint8_t)(rnd() % 128); pt->grids.chaos = (uint8_t)(rnd() % 128); }
        if (r == 8 && seq_song_entry(&S) < 0) {                 /* the song again, from the start */
            int n;
            seq_stop(&S, out, &n);
            for (int i = 0; i < n; i++) assert(c[out[i].track][out[i].note]-- > 0);
            for (int t2 = 0; t2 < Q_TRACKS; t2++) {
                int sum = 0;
                for (int k = 0; k < 128; k++) { assert(c[t2][k] >= 0); sum += c[t2][k]; }
                assert(sum == legatos[t2]);                     /* a legato's tied note had no note-off */
            }
            seq_start(&S, &P, (int)(rnd() % 2));
        }
        if (r == 4) {
            pt->path = (uint8_t)(rnd() % PATH_N);
            pt->xlen = (uint8_t)(1 + rnd() % 4);
            pt->ylen = (uint8_t)(1 + rnd() % 4);
            pt->ydiv = (uint8_t)(rnd() % DIV_N);
        }
        q_event ev[64];
        int n = seq_block(&S, &P, 1 + rnd() % 512, ev, 2 + (int)(rnd() % 63));
        started += seq_scene_started(&S) >= 0;
        int e = seq_song_entry(&S);
        entries += e >= 0 && e != entry;
        entry = e;
        for (int i = 0; i < n; i++) {
            q_event *e = &ev[i];
            if (e->track == Q_CLICK_TRACK) continue;
            int sum = 0;
            for (int k = 0; k < 128; k++) sum += c[e->track][k];
            if (e->on && (e->flags & EV_LEGATO)) { assert(sum > legatos[e->track]); legatos[e->track]++; }
            if (e->on) { c[e->track][e->note]++; ons++; gens += P.pat[e->track][e->pat].src != SRC_STEP; }
            else assert(c[e->track][e->note]-- > 0);
        }
        for (int t2 = 0; t2 < Q_TRACKS; t2++) {
            int held = 0;
            for (int i = 0; i < S.q[t2].n; i++) {
                ties += S.q[t2].e[i].e.on && S.q[t2].e[i].gate == TIED;
                held += S.q[t2].e[i].at == HELD;
            }
            assert(held <= 1);
            /* the tie chain: each queued legato replaces the one tie pending before it, a tied note-on is pending
               after it; the tie left pending is the track's */
            int pending = held;
            for (int i = 0; i < S.q[t2].n; i++) {
                const q_queued *e = &S.q[t2].e[i];
                if (!e->e.on) continue;
                if (e->e.flags & EV_LEGATO) { assert(pending); pending = 0; }
                if (e->gate == TIED) { assert(!pending); pending = 1; }
            }
            assert(pending == S.t[t2].tie);
        }
    }
    int n;
    seq_stop(&S, out, &n);
    for (int i = 0; i < n; i++) assert(c[out[i].track][out[i].note]-- > 0);
    for (int t = 0; t < Q_TRACKS; t++) {
        int sum = 0;
        for (int k = 0; k < 128; k++) { assert(c[t][k] >= 0); sum += c[t][k]; }
        assert(sum == legatos[t]);
    }
    assert(ons > 400 && ties > 0 && legatos[0] + legatos[1] + legatos[2] + legatos[3] > 10);
    assert(launches > 100 && started > 30 && entries > 30);
    assert(srcs > 100 && gens > 400);
}

/* track 0's pattern 0 a GRIDS pattern, every density full */
static void grids_setup(void)
{
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_GRIDS;
    for (int i = 0; i < 3; i++) pt->grids.dens[i] = 127;
}

static void grids_steps_on_grid(void)
{
    grids_setup();
    P.swing = 60;
    run_to((uint64_t)at16(70, 0));
    int ons = 0, loop = 0, last = -1;
    for (int i = 0; i < NEV; i++) {
        if (!EV[i].e.on) continue;
        int k = EV[i].e.step;
        if (k <= last) loop++;
        last = k;
        assert(k < 32 && (int64_t)EV[i].at == at16(32 * loop + k, k & 1 ? 960 : 0));
        ons++;
    }
    assert(loop == 2 && ons > 30);
}

static void grids_accent_velocity(void)
{
    int mixed = 0, ons = 0;
    grids_setup();
    P.sound[0].model = M_KIT;
    run_to((uint64_t)at16(32, 0));
    for (int k = 0; k < 32; k++) {
        int acc = 0, plain = 0;
        for (int i = 0; i < NEV; i++) {
            if (!EV[i].e.on || EV[i].e.step != k) continue;
            int v = EV[i].e.note - KIT_NOTE, level = grids_read(k, v, grids_u8(64), grids_u8(64));
            assert(EV[i].e.vel == (level > 192 ? 127 : 95));
            level > 192 ? acc++ : plain++;
            ons++;
        }
        mixed += acc && plain;
    }
    assert(mixed && ons > 40);
}

static void generator_pattern_change_at_loop_end(void)
{
    setup();
    P.pat[0][1].src = SRC_GRIDS;
    run_to((uint64_t)at16(3, 0));
    P.pattern[0] = 1;
    run_to((uint64_t)at16(16, 0) - 1);
    assert(seq_pattern(&S, 0) == 0 && seq_pos(&S, 0) == 15);
    run_to((uint64_t)at16(16, 0));
    assert(seq_pattern(&S, 0) == 1 && seq_pos(&S, 0) == 16);   /* its 32 steps counted from PLAY */
    run_to((uint64_t)at16(19, 0));
    P.pattern[0] = 0;
    run_to((uint64_t)at16(32, 0) - 1);
    assert(seq_pattern(&S, 0) == 1 && seq_pos(&S, 0) == 31);
    run_to((uint64_t)at16(32, 0));
    assert(seq_pattern(&S, 0) == 0 && seq_pos(&S, 0) == 0);
}

static void marbles_len_sets_loop(void)
{
    setup();
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.len = 5;
    for (int k = 0; k < 21; k++) {
        run_to((uint64_t)at16(k, 0) + 1);
        assert(seq_pos(&S, 0) == k % 5);
    }
}

/* track 0 a MARBLES pattern whose loops mutate every step, a note every step; muted or not, then the notes of the
   note-ons from step 16 to 20 */
static void marbles_from_16(int mute, int *notes)
{
    int n = 0;
    setup();
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.tout = 2;
    P.pat[0][0].marbles.dejavu = 0;
    P.pat[0][0].marbles.spread = 100;
    P.mute = (uint8_t)mute;
    run_to((uint64_t)at16(15, 0));                              /* steps 0..15 planned */
    P.mute = 0;
    run_to((uint64_t)at16(20, 1));
    for (int i = 0; i < NEV; i++) {
        if (!EV[i].e.on || (int64_t)EV[i].at < at16(16, 0)) continue;
        assert(n < 5);
        notes[n++] = EV[i].e.note;
    }
    assert(n == 5);
}

/* a muted generator still draws its steps: unmuted, it plays what it would have played */
static void muted_generator_advances(void)
{
    int want[5], got[5], ons = 0;
    marbles_from_16(0, want);
    marbles_from_16(1, got);
    for (int i = 0; i < NEV; i++) ons += EV[i].e.on;
    assert(ons == 5);                                           /* nothing before step 16 */
    for (int i = 0; i < 5; i++) assert(got[i] == want[i]);
}

static void source_change_mid_play(void)
{
    static int c[128];
    q_event out[Q_TRACKS * SEQ_QUEUE];
    int n, gen = 0;
    setup();
    memset(c, 0, sizeof c);
    run_to((uint64_t)at16(10, 0));
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.len = 3;
    P.pat[0][0].marbles.tout = 2;                               /* a note every step */
    run_to((uint64_t)at16(11, 0));
    assert(seq_pos(&S, 0) == 11 % 3);                           /* steps since PLAY modulo LEN */
    run_to((uint64_t)at16(20, 0) + 1);
    assert(seq_pos(&S, 0) == 20 % 3);
    for (int i = 0; i < NEV; i++) {
        if (EV[i].e.on) { c[EV[i].e.note]++; gen += (int64_t)EV[i].at >= at16(11, 0); }
        else assert(c[EV[i].e.note]-- > 0);
    }
    seq_stop(&S, out, &n);
    for (int i = 0; i < n; i++) assert(c[out[i].note]-- > 0);
    for (int k = 0; k < 128; k++) assert(c[k] == 0);
    assert(gen >= 9);
}

/* GRIDS turned STEP between plan()'s loop length and its step: rank 31 still a generator's step, never REV's 16 - 1 - 31 */
static void source_read_once_per_step(void)
{
    grids_setup();
    P.pat[0][0].path = PATH_REV;
    P.pat[0][0].len = 16;
    while (S.t[0].path != PATH_REV || S.t[0].h[1].rank != 30) run(1);   /* the path is taken at the wrap */
    flip_pt = &P.pat[0][0];
    while (S.t[0].h[1].rank == 30) {         /* GRIDS again until the flip lands in the iteration planning rank 31 */
        P.pat[0][0].src = SRC_GRIDS;
        block(1, 64);
    }
    flip_pt = 0;
    assert(P.pat[0][0].src == SRC_STEP && S.t[0].h[1].rank == 31 && S.t[0].h[1].step == 31);
    while (S.t[0].h[1].rank == 31) run(1);
    assert(S.t[0].h[1].rank == 0 && S.t[0].h[1].step == 15);   /* then STEP's REV */
}

static void song_entry_counts_loop(void)
{
    int starts = 0;
    setup();
    P.pat[0][1].src = SRC_GRIDS;
    P.scene[0] = (q_scene){ 1, 0, { 1, 0, 0, 0 } };
    P.song[0] = (q_entry){ 0, 1 };
    P.nsong = 1;
    P.song_on = 1;
    seq_start(&S, &P, 0);
    while (T < (uint64_t)at16(40, 0)) {
        run(1);
        if (seq_scene_started(&S) < 0) continue;
        if (starts++ == 1) assert((int64_t)T - BLK <= at16(32, 0) && at16(32, 0) < (int64_t)T);   /* 2 bars */
    }
    assert(starts == 2);
}

/* a STEP pattern's entry counts its len, XY included */
static void song_entry_counts_xy_len(void)
{
    int starts = 0;
    setup();
    P.pat[0][1].path = PATH_XY;
    P.pat[0][1].len = 32;
    P.pat[0][1].xlen = 2;
    P.scene[0] = (q_scene){ 1, 0, { 1, 0, 0, 0 } };
    P.song[0] = (q_entry){ 0, 1 };
    P.nsong = 1;
    P.song_on = 1;
    seq_start(&S, &P, 0);
    while (T < (uint64_t)at16(40, 0)) {
        run(1);
        if (seq_scene_started(&S) < 0) continue;
        if (starts++ == 1) assert((int64_t)T - BLK <= at16(32, 0) && at16(32, 0) < (int64_t)T);   /* 2 bars */
    }
    assert(starts == 2);
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

/* the bar's position at the block's start, from the beat clock */
static void bar_frames(void)
{
    setup();
    assert(P.tempo == 120);
    block(BLK, 64);                         /* the first block sets the beat clock */
    run(2);
    assert(fabsf(seq_bar_frames(&S) - 768.0f) <= 1.0f);
    run_to(88236);                          /* 4 beats of 22058.8 */
    assert(seq_bar_frames(&S) >= 0.0f && seq_bar_frames(&S) <= 2.0f);
    run_to(88236 + 22059);
    assert(fabsf(seq_bar_frames(&S) - 22059.0f) <= 2.0f);
    S.running = 0;
    assert(seq_bar_frames(&S) == -1.0f);
    setup();
    seq_start(&S, &P, 1);                   /* a bar of count-in */
    for (int k = 0; k < 340; k++) {         /* 340 x 256 < 88235 */
        assert(seq_bar_frames(&S) == -1.0f);
        block(BLK, 64);
    }
    run_to(88236 + 512);
    assert(fabsf(seq_bar_frames(&S) - 512.0f) <= 2.0f);
}

/* a MARBLES track's notes, decided a step ahead for the scene, sound at the frames they always did: the hash of
   (frame, note, velocity, on) over 600 blocks. With two MARBLES tracks (T OUT 1 at 1/16 and 3 at 1/8) the frames
   alone (the tracks share the random draws, whose order the earlier planning changes); one track alone, the notes too */
static uint32_t marbles_hash(int frames_only, int tracks)
{
    uint32_t h = 2166136261u;
    setup();
    for (int t = 0; t < tracks; t++) {
        P.pat[t][0].src = SRC_MARBLES;
        P.pat[t][0].marbles.tout = (uint8_t)(t ? 3 : 1);
        P.pat[t][0].marbles.spread = 90;
    }
    if (tracks > 1) P.pat[1][0].div = DIV_8;
    run(600);
    for (int i = 0; i < NEV; i++) {
        if (EV[i].e.track > 1) continue;
        uint32_t v[4] = {(uint32_t)EV[i].at, frames_only ? 0u : EV[i].e.note, frames_only ? 0u : EV[i].e.vel, EV[i].e.on};
        for (int k = 0; k < 4; k++) h = (h ^ v[k]) * 16777619u;
    }
    return h;
}

static void marbles_timing_unchanged(void)
{
    assert(marbles_hash(1, 2) == 0xbef1ab75u);      /* the values before the notes were decided a step ahead */
    assert(marbles_hash(0, 1) == 0x628f3206u);
}

/* the scene's notes: a MARBLES note is known with its frame at least a step (less a block) before it sounds */
static void marbles_notes_known_a_step_ahead(void)
{
    uint32_t seen = 0, at[16], kat[64];
    uint8_t notes[16], knote[64];
    int got = 0;
    setup();
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.tout = 1;
    seq_step_ratio(P.tempo, P.pat[0][0].div, &N16, &D16);
    while (got < 32) {                  /* read after every block */
        block(BLK, 64);
        int n = seq_gen_notes(&S, 0, &seen, notes, at, 16);
        for (int i = 0; i < n; i++, got++) {
            if (at[i] >= N16 / D16)       /* (the first step is planned at the start: no step before it) */
                assert((uint64_t)at[i] >= T + N16 / D16 - BLK);
            knote[got] = notes[i];
            kat[got] = at[i];
        }
    }
    run_to(kat[got - 1] + 1);
    for (int k = 0; k < got; k++) {     /* each sounds at its frame */
        int found = 0;
        for (int e = 0; e < NEV; e++)
            found |= EV[e].e.track == 0 && EV[e].e.on && EV[e].at == kat[k] && EV[e].e.note == knote[k];
        assert(found);
    }
    uint32_t s1 = 0;
    assert(seq_gen_notes(&S, 1, &s1, notes, at, 16) == 0);  /* a STEP track: none */
}

/* GRIDS notes reach the scenes' ring too */
static void grids_notes_in_ring(void)
{
    uint32_t seen = 0, at[16];
    uint8_t notes[16];
    int got = 0;
    setup();
    P.pat[0][0].src = SRC_GRIDS;
    P.pat[0][0].grids.dens[0] = P.pat[0][0].grids.dens[1] = P.pat[0][0].grids.dens[2] = 127;
    for (int b = 0; b < 1500; b++) { block(BLK, 64); got += seq_gen_notes(&S, 0, &seen, notes, at, 16); }   /* ~70 steps */
    assert(got > 5);
}

/* CHORDS' STRUM: a strike's notes one after the other, lowest first, STRUM / 24 of a step apart */
static void chords_setup_seq(int rate, int strum)
{
    setup();
    for (int t = 1; t < Q_TRACKS; t++) for (int i = 0; i < Q_STEPS; i++) P.pat[t][0].steps[i].flags = 0;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_CHORDS;
    pt->chords.rate = (uint8_t)rate; pt->chords.strum = (uint8_t)strum; pt->chords.voice = 0;
    P.sound[0].poly = 3;
    seq_start(&S, &P, 0);
}
static int nth_note(int k)
{
    for (int i = 0; i < NEV; i++)
        if (EV[i].e.track == 0 && EV[i].e.on && k-- == 0) return EV[i].e.note;
    return -1;
}
static void chords_strum_staggers(void)
{
    chords_setup_seq(0, 3);
    run(40);
    for (int i = 0; i < 3; i++) assert(nth(0, 1, i) == at16(0, 600 * i));
    assert(nth_note(0) < nth_note(1) && nth_note(1) < nth_note(2));
}
static void chords_strum_zero(void)
{
    chords_setup_seq(0, 0);
    run(40);
    for (int i = 0; i < 3; i++) assert(nth(0, 1, i) == at16(0, 0));
}
static void chords_strum_overlaps(void)
{
    chords_setup_seq(4, 7);
    run(40);
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 3; i++) assert(nth(0, 1, 3 * k + i) == at16(k, 1400 * i));
}
/* the live transposition: semitones, or degrees of the scale (C major: E up 2 degrees is G); a KIT stays */
static void transpose_moves_notes(void)
{
    static const struct { int d, deg, model, note, want; } c[] = {
        { 3, 0, 8, 64, 67 }, { -5, 0, 8, 64, 59 }, { 2, 1, 8, 64, 67 }, { -3, 1, 8, 64, 59 }, { 1, 1, 8, 63, 64 },
        { 7, 1, 8, 60, 72 }, { 5, 0, M_KIT, KIT_NOTE + 2, KIT_NOTE + 2 }, { 100, 0, 8, 64, 127 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        setup();
        only_step(0, 0);
        P.sound[0].model = (uint8_t)c[i].model;
        P.pat[0][0].steps[0].note[0] = (uint8_t)c[i].note;
        S.transpose = c[i].d;
        S.tdeg = c[i].deg;
        run(30);
        int on = -1, off = -1;
        for (int j = 0; j < NEV; j++) {
            if (EV[j].e.track != 0) continue;
            if (EV[j].e.on && on < 0) on = EV[j].e.note;
            if (!EV[j].e.on && off < 0) off = EV[j].e.note;
        }
        assert(on == c[i].want && off == c[i].want);
    }
}

/* a STEP pattern printed from a generator keeps its strum */
static void step_plays_strum_bits(void)
{
    setup();
    only_step(0, 2);
    q_step *st = &P.pat[0][0].steps[2];
    st->note[1] = 64; st->note[2] = 67; st->flags |= 2 << 5;
    P.sound[0].poly = 3;
    run(80);
    for (int i = 0; i < 3; i++) assert(nth(0, 1, i) == at16(2, 400 * i));
}

/* PRINT: the generator's next pass from rank 0, each step as played (the notes the queue got), then nothing more */
static void print_sends_next_pass(void)
{
    q_print r[16];
    uint32_t seen = 0;
    int n = 0;
    setup();
    P.pat[0][0].src = SRC_GRIDS;
    P.pat[0][0].grids.len = 8;
    P.pat[0][0].grids.dens[0] = 127;
    seq_start(&S, &P, 0);
    run_to((uint64_t)at16(3, 0));
    seq_print(&S, 0, 0);
    run_to((uint64_t)at16(7, 0));
    assert(!seq_printed(&S, &seen, &r[0]));             /* not before the next pass */
    while (T < (uint64_t)at16(17, 0)) {
        run(1);
        while (n < 16 && seq_printed(&S, &seen, &r[n])) n++;
    }
    assert(n == 8 && S.print == 0);
    for (int k = 0; k < 8; k++) {
        assert(r[k].k == k && r[k].n == 8);
        int on = 0;
        for (int i = 0; i < NEV; i++)
            if (EV[i].e.on && EV[i].e.step == k && (int64_t)EV[i].at >= at16(8, 0) && (int64_t)EV[i].at < at16(16, 0)) on++;
        assert((r[k].st.flags & SF_ON) ? on > 0 : on == 0);
    }
    run(300);
    assert(S.pr_w == 8);                                /* one pass only */
}

/* SOURCE switched away and back while playing: the generator starts over, its bars on the tour (CHORDS, TONNETZ) */
static void source_back_restarts_generator(void)
{
    chords_setup_seq(0, 0);                          /* CHORDS, a strike a bar */
    q_pattern *pt = &P.pat[0][0];
    run(110);                                        /* about 5 steps into bar 0 (a step is about 21 blocks) */
    pt->src = SRC_STEP;
    run(90);                                         /* about 4 steps of STEP */
    pt->src = SRC_CHORDS;
    NEV = 0;
    run(60);
    assert(S.g[0].chords.n % 16 == (uint32_t)(S.t[0].k + 1) % 16);   /* the counter on the tour's rank again */
}

/* pads 10-12 (BD2, SN2, CH2) play the synthetic drums, the others the analog ones */
static void kit_default_char(void)
{
    project_defaults(&P);
    for (int t = 0; t < Q_TRACKS; t++)
        for (int k = 0; k < Q_PADS; k++)
            assert(P.kit[t][k].chr == (k >= 9 && k <= 11 ? 127 : 0));
}

int main(void)
{
    kit_default_char();
    marbles_timing_unchanged();
    marbles_notes_known_a_step_ahead();
    grids_notes_in_ring();
    bar_frames();
    step_frames_values();
    steps_land_on_exact_frames();
    polyrhythm();
    length_shrink_wraps();
    length_back_realigns();
    length_doubled_goes_on();
    jump_keeps_place();
    switch_lands_on_play_grid();
    tempo_change_keeps_grid();
    tempo_changes_keep_tracks_aligned();
    division_change_mid_play();
    stop_releases_notes();
    events_sorted();
    length_sets_gate();
    micro_shifts_note();
    chords_strum_staggers();
    chords_strum_zero();
    chords_strum_overlaps();
    step_plays_strum_bits();
    transpose_moves_notes();
    print_sends_next_pass();
    source_back_restarts_generator();
    swing_delays_odd_steps();
    ratchet_repeats();
    repeat_flag_on_ratchet_repeats();
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
    xy_tempo_change_keeps_rows();
    overflow_stays_queued();
    count_in_delays_tracks();
    click_follows_flag();
    locate_and_quantize();
    cond_ab();
    first_and_not_first();
    pre_follows_last_condition();
    probability_rates();
    mute_silences_but_advances();
    accent_boosts_velocity();
    accent_flag_on_accented_notes();
    slide_ties_to_next();
    stop_during_tie_releases();
    events_carry_step();
    scene_launch_at_next_bar();
    scene_launch_keeps_offs();
    scene_launch_step_to_grids();
    scene_then_project_copy_is_no_request();
    song_advances_and_loops();
    song_with_count_in();
    every_on_gets_its_off();
    grids_steps_on_grid();
    grids_accent_velocity();
    generator_pattern_change_at_loop_end();
    marbles_len_sets_loop();
    muted_generator_advances();
    source_change_mid_play();
    source_read_once_per_step();
    song_entry_counts_loop();
    song_entry_counts_xy_len();
    lock_set_find_clear();
    lock_set_overwrites();
    lock_full_refused();
    puts("seq_test ok");
    return 0;
}

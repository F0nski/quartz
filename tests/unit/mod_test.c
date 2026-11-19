#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "mod.c"
#include "gen.c"
#include "seq.c"

static q_vparam dflt(void) { return (q_vparam){ 0, 64, 127, 20, FT_LP, 127, 0, 0, LS_SIN, 0, 5, 0, LD_CUT }; }
static pq_mod blk(int v, const q_vparam *p, int tempo) { pq_mod m; mod_set(v, p, tempo); mod_voice_block(v, &m); return m; }
static float semis(float cutoff) { return 12 * log2f(cutoff * Q_SR / 440) + 69; }
static const float DT = MOD_BLK / Q_SR;

static void setup(void) { mod_init(); }

static void adsr_default_is_gate(void)
{
    setup(); q_vparam p = dflt(); mod_set(0, &p, 120);
    mod_note_on(0, 0);
    int a = 1; float prev = blk(0, &p, 120).env;       /* ATTACK 0 rises in 1 ms: a jump to 1 would click */
    assert(prev < 0.5f);
    while (prev < 1) { float e = blk(0, &p, 120).env; assert(e > prev); prev = e; a++; }
    assert(fabsf(a * DT - 0.001f) < DT);
    for (int i = 0; i < 100; i++) assert(blk(0, &p, 120).env == 1);
    mod_note_off(0);
    int n = 0; while (blk(0, &p, 120).env >= 0.001f) n++;
    assert(n * DT < 0.005f);
    assert(blk(0, &p, 120).env == 0);
}

static void adsr_times(void)
{
    setup(); q_vparam p = dflt(); p.atk = 64; p.dec = 64; p.sus = 64; p.rel = 64;
    float t = 0.001f * powf(10000.f, 64 / 127.f);
    mod_set(0, &p, 120); mod_note_on(0, 0);
    int n = 0; float prev = 0;
    for (;;) { float e = blk(0, &p, 120).env; assert(e >= prev); prev = e; n++; if (e >= 1) break; assert(n < 100000); }
    assert(fabsf(n * DT - t) < 2 * DT);
    float sus = 64 / 127.f; int d = 0; float e;
    do { e = blk(0, &p, 120).env; d++; } while (d * DT < t);
    assert(fabsf(e - sus - 0.01f * (1 - sus)) < 0.002f);
    mod_note_off(0);
    int r = 0; do { e = blk(0, &p, 120).env; r++; assert(r < 100000); } while (e >= 0.001f);
    assert(fabsf(r * DT - t * logf(sus / 0.001f) / logf(1000.f)) < 2 * DT);   /* from sus, not from 1 */
}

static void adsr_release_from_attack(void)
{
    setup(); q_vparam p = dflt(); p.atk = 64; p.rel = 64; mod_set(0, &p, 120); mod_note_on(0, 0);
    float e = 0; for (int i = 0; i < 190; i++) e = blk(0, &p, 120).env;
    assert(e > 0.3f && e < 0.7f);
    mod_note_off(0);
    float e2 = blk(0, &p, 120).env;
    assert(e2 <= e && e - e2 < 0.01f);
    assert(blk(0, &p, 120).env < e2);
}

static void adsr_attack_from_current_level(void)
{
    setup(); q_vparam p = dflt(); p.atk = 64; p.rel = 64; mod_set(0, &p, 120);
    mod_note_on(0, 0); for (int i = 0; i < 1000; i++) blk(0, &p, 120);
    mod_note_off(0);
    float e = 1; for (int i = 0; i < 60; i++) e = blk(0, &p, 120).env;
    assert(e > 0.2f && e < 0.9f);
    mod_note_on(0, 0);
    float e2 = blk(0, &p, 120).env, step = DT / env_time(64);
    assert(e2 >= e && e2 - e <= step);
    assert(blk(0, &p, 120).env > e2);
}

static void filter_env_amount_and_accent(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 0; p.envamt = 63; p.sus = 127;
    mod_set(0, &p, 120); mod_note_on(0, 0);
    for (int i = 0; i < 4; i++) blk(0, &p, 120);      /* past the 1 ms attack */
    float base = 15.5f;
    assert(fabsf(semis(blk(0, &p, 120).cutoff) - (base + 96)) < 0.01f);
    mod_note_on(0, 1);
    pq_mod m = blk(0, &p, 120);
    assert(m.cutoff == 0.45f);                        /* 15.5 + 144 is far above the bound */
    p.cutoff = 64; p.envamt = 20; mod_set(0, &p, 120); mod_note_on(0, 0);
    float s0 = 15.5f + 64 * 119.5f / 127;
    assert(fabsf(semis(blk(0, &p, 120).cutoff) - (s0 + 20 / 63.f * 96)) < 0.01f);
    mod_note_on(0, 1);
    assert(fabsf(semis(blk(0, &p, 120).cutoff) - (s0 + 20 / 63.f * 144)) < 0.01f);
    p.cutoff = 127; p.envamt = 63; mod_set(0, &p, 120);
    assert(blk(0, &p, 120).cutoff == 0.45f);
}

/* cutoff offset in semitones at an LFO phase reached through the FREE clock */
static float lfo_at(int shape, double quarters)
{
    mod_init(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = shape; p.ldepth = 63; p.lrate = 5;   /* 1 beat */
    mod_clock(120, (uint32_t)(quarters * 0.125 * Q_SR + 0.5));
    for (int i = 0; i < 100; i++) blk(0, &p, 120);  /* the glide from 0 settled (FREE: mod_set holds the phase) */
    return semis(blk(0, &p, 120).cutoff) - (15.5f + 60 * 119.5f / 127);
}
static void lfo_shapes(void)
{
    const float sin_[4] = { 0, 60, 0, -60 }, tri[4] = { 0, 60, 0, -60 }, saw[4] = { -60, -30, 0, 30 };
    for (int q = 0; q < 4; q++) {
        assert(fabsf(lfo_at(LS_SIN, q) - sin_[q]) < 0.5f);
        assert(fabsf(lfo_at(LS_TRI, q) - tri[q]) < 0.5f);
        assert(fabsf(lfo_at(LS_SAW, q) - saw[q]) < 0.5f);
    }
    assert(fabsf(lfo_at(LS_SQR, 0) - 60) < 0.01f);
    assert(fabsf(lfo_at(LS_SQR, 1) - 60) < 0.01f);
    assert(fabsf(lfo_at(LS_SQR, 3) + 60) < 0.01f);
    assert(fabsf(lfo_at(LS_SQR, 5) - 60) < 0.01f);   /* a quarter into the 2nd cycle: +60 */
}

/* SAW's return and SQR's edges are jumps: the LFO glides over them in about 2 ms, or they click */
static void lfo_jumps_softened(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = LS_SAW; p.ldepth = 63; p.lrate = 5;   /* 1 beat */
    float base = 15.5f + 60 * 119.5f / 127;
    mod_clock(120, (uint32_t)(0.5f * Q_SR) - 2 * MOD_BLK);                  /* just before the wrap */
    for (int i = 0; i < 60; i++) blk(0, &p, 120);                           /* FREE: mod_set holds the phase */
    float top = semis(blk(0, &p, 120).cutoff) - base;
    assert(top > 59);
    mod_clock(120, 4 * MOD_BLK);                                            /* past it: -60 is the target */
    float first = semis(blk(0, &p, 120).cutoff) - base;
    assert(first < top && top - first < 0.2f * 120);
    int n = 1; float e = first;
    while (e > -60 + 0.05f * 120) { e = semis(blk(0, &p, 120).cutoff) - base; n++; assert(n < 100); }
    assert(n * DT > 0.004f && n * DT < 0.008f);                           /* 95 % in 3 time constants of 2 ms */
}

static void lfo_free_shared_phase(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = LS_SAW; p.ldepth = 63; p.lrate = 5;
    mod_set(0, &p, 120); mod_set(1, &p, 120);
    mod_note_on(0, 0);
    for (int i = 0; i < 300; i++) { mod_clock(120, MOD_BLK); blk(0, &p, 120); blk(1, &p, 120); }
    mod_note_on(1, 0);
    pq_mod a = blk(0, &p, 120), b = blk(1, &p, 120);
    assert(a.cutoff == b.cutoff);
    mod_restart();
    for (int i = 0; i < 100; i++) a = blk(0, &p, 120);
    assert(fabsf(semis(a.cutoff) - (15.5f + 60 * 119.5f / 127) + 60) < 0.01f);   /* SAW at phase 0: -1, once glided to */
}

static void lfo_trig_restarts(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.ltrig = 1; p.ldepth = 63; p.lrate = 5;
    mod_set(0, &p, 120); mod_note_on(0, 0);
    float s60 = 15.5f + 60 * 119.5f / 127;
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 400 + 111 * k; i++) mod_clock(120, MOD_BLK), blk(0, &p, 120);
        assert(fabsf(semis(blk(0, &p, 120).cutoff) - s60) > 5);
        float before = fabsf(semis(blk(0, &p, 120).cutoff) - s60);
        mod_set(0, &p, 120); mod_note_on(0, 0);
        assert(mod_lfo_phase(0) == 0);                                  /* sin 0, glided to */
        assert(fabsf(semis(blk(0, &p, 120).cutoff) - s60) < before);
    }
}

static void lfo_phase_advances(void)
{
    setup(); q_vparam p = dflt(); p.ltrig = 1; p.lrate = 5;
    mod_set(0, &p, 120); mod_note_on(0, 0);
    assert(mod_lfo_phase(0) == 0);
    for (int i = 0; i < 10; i++) blk(0, &p, 120);
    assert(mod_lfo_phase(0) > 0 && mod_lfo_phase(0) < 1);
    mod_set(0, &p, 120); mod_note_on(0, 0);
    assert(mod_lfo_phase(0) == 0);
}

static void lfo_sync_tempo_change_continuous(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = LS_SAW; p.ldepth = 63; p.lrate = 5;
    float prev = -1; int tempo = 120;
    for (int i = 0; i < 6000; i++) {
        if (i == 3000) tempo = 90;
        blk(0, &p, tempo);
        float ph = mod_lfo_phase(0);                /* the phase itself: the value glides after each wrap */
        if (prev >= 0 && prev - ph < 0.5f) assert(fabsf(ph - prev) < 0.001f);   /* a wrap goes down by ~1 */
        prev = ph;
        mod_clock(tempo, MOD_BLK);
    }
}

static void lfo_level_depth(void)
{
    setup(); q_vparam p = dflt(); p.ldest = LD_LEVEL; p.lshape = LS_SIN; p.lrate = 5;
    p.ldepth = 0; assert(blk(0, &p, 120).gain == 1);
    p.ldepth = -63; float lo = 2, hi = -1;
    for (int i = 0; i < 2000; i++) { mod_clock(120, MOD_BLK); float g = blk(0, &p, 120).gain; if (g < lo) lo = g; if (g > hi) hi = g; }
    assert(lo >= 0 && lo < 0.01f && hi <= 1 && hi > 0.99f);
}

static void lfo_pitch_range(void)
{
    setup(); q_vparam p = dflt(); p.ldest = LD_PITCH; p.ldepth = 63; p.lrate = 5;
    float lo = 99, hi = -99;
    for (int i = 0; i < 2000; i++) { mod_clock(120, MOD_BLK); float x = blk(0, &p, 120).pitch; if (x < lo) lo = x; if (x > hi) hi = x; }
    assert(lo >= -12 && lo < -11.9f && hi <= 12 && hi > 11.9f);
}

static void sh_changes_once_per_cycle(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = LS_SH; p.ldepth = 63; p.lrate = 5; p.ltrig = 1;
    mod_set(0, &p, 120); mod_note_on(0, 0);
    blk(0, &p, 120);
    int changes = 0; float prev = lfo_value(&MV[0], 0);                /* its own value, not the glide toward it */
    for (int i = 0; i < 3 * 1838 - 20; i++) { blk(0, &p, 120); float c = lfo_value(&MV[0], 0); if (c != prev) changes++; prev = c; }
    assert(changes == 2);
    /* FREE: voices of a track share the value */
    p.ltrig = 0; mod_set(0, &p, 120); mod_set(1, &p, 120); mod_clock(120, 30000);
    pq_mod a, b;
    for (int i = 0; i < 100; i++) { a = blk(0, &p, 120); b = blk(1, &p, 120); }   /* both glided to it */
    assert(fabsf(semis(a.cutoff) - semis(b.cutoff)) < 0.01f);
}

/* arpeggiator: a log of the events with absolute frames */
typedef struct { uint64_t t; q_event e; } alog;
static alog LOG[1024]; static int NLOG; static uint64_t NOW;
static uint32_t NUM, DEN;
static void asetup(q_arp *a, int tempo) { arp_reset(a); NLOG = 0; NOW = 0; seq_step_ratio(tempo, DIV_16, &NUM, &DEN); }
static void ain(q_arp *a, int on, int note, uint32_t off, int flags)
{ q_event e = { off, 0, (uint8_t)note, 100, (uint8_t)on, 1, 2, (uint8_t)flags, 0 }; arp_input(a, &e); }
static void arun(q_arp *a, const q_arpp *p, uint32_t frames, uint32_t blk)
{
    for (uint32_t d = 0; d < frames; d += blk) {
        q_event o[ARP_OUT]; int n = arp_block(a, p, NUM, DEN, blk, o);
        assert(n <= ARP_OUT);
        for (int i = 0; i < n; i++) { assert(i == 0 || o[i].offset >= o[i - 1].offset); LOG[NLOG].t = NOW + o[i].offset; LOG[NLOG++].e = o[i]; }
        NOW += blk;
    }
}
static int aons(int *notes) { int c = 0; for (int i = 0; i < NLOG; i++) if (LOG[i].e.on) notes[c++] = LOG[i].e.note; return c; }
static uint32_t at(int k) { return (uint32_t)((uint64_t)k * NUM / DEN); }
static q_arpp ap(int mode, int oct, int gate) { return (q_arpp){ (uint8_t)mode, (uint8_t)oct, DIV_16, (uint8_t)gate }; }
static void hold3(q_arp *a) { ain(a, 1, 60, 0, 0); ain(a, 1, 64, 0, 0); ain(a, 1, 67, 0, 0); }
static void expect_notes(const int *want, int n)
{
    int got[256], c = aons(got);
    assert(c >= n);
    for (int i = 0; i < n; i++) { if (got[i] != want[i]) printf("step %d: %d != %d\n", i, got[i], want[i]); assert(got[i] == want[i]); }
}
/* every note-on has its note-off, and no note is on twice at once */
static void check_paired(void)
{
    int on[128] = {0};
    for (int i = 0; i < NLOG; i++) { q_event *e = &LOG[i].e; if (e->on) assert(!on[e->note]++); else assert(on[e->note]-- == 1); }
    for (int i = 0; i < 128; i++) assert(!on[i]);
}

static void arp_up_two_octaves(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 2, 50); hold3(&a);
    arun(&a, &p, 7 * 5515 + 100, 1000);
    static const int w[] = { 60, 64, 67, 72, 76, 79, 60 }; expect_notes(w, 7);
    int k = 0;
    for (int i = 0; i < NLOG; i++) if (LOG[i].e.on && k < 7) {
        assert(LOG[i].t == at(k)); assert(LOG[i].e.vel == 100 && LOG[i].e.pat == 1 && LOG[i].e.step == 2);
        int j = i + 1; while (LOG[j].e.on || LOG[j].e.note != LOG[i].e.note) j++;
        assert(LOG[j].t == LOG[i].t + (uint32_t)((uint64_t)NUM * 50 / (100ull * DEN))); k++;
    }
}
static void arp_down(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_DOWN, 1, 50); hold3(&a); arun(&a, &p, 5 * 5515, 1000);
    static const int w[] = { 67, 64, 60, 67, 64 }; expect_notes(w, 5);
}
static void arp_updown_no_repeat(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UPDN, 1, 50); hold3(&a); arun(&a, &p, 9 * 5515, 1000);
    static const int w[] = { 60, 64, 67, 64, 60, 64, 67, 64, 60 }; expect_notes(w, 9);
}
static void arp_order_follows_arrival(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_ORDER, 1, 50);
    ain(&a, 1, 67, 0, 0); ain(&a, 1, 60, 0, 0); ain(&a, 1, 64, 0, 0); arun(&a, &p, 4 * 5515, 1000);
    static const int w[] = { 67, 60, 64, 67 }; expect_notes(w, 4);
}
static void arp_rand_in_set(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_RAND, 1, 50); hold3(&a); arun(&a, &p, 64 * 5515 + 10, 1000);
    int got[256], c = aons(got), seen[3] = {0}; assert(c >= 64);
    for (int i = 0; i < 64; i++) { assert(got[i] == 60 || got[i] == 64 || got[i] == 67); seen[(got[i] - 60) / 3]++; }
    assert(seen[0] && seen[1] && seen[2]);
}
static void arp_single_note_repeats(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 0, 0); arun(&a, &p, 4 * 5515, 1000);
    static const int w[] = { 62, 62, 62, 62 }; expect_notes(w, 4);
}
static void arp_starts_at_first_note(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 100, 0);
    arun(&a, &p, 3000, 1000);
    assert(NLOG > 0 && LOG[0].e.on && LOG[0].t == 100);
}
static void arp_gate_min(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 5); ain(&a, 1, 62, 0, 0); arun(&a, &p, 3000, 1000);
    assert(NLOG >= 2 && LOG[1].e.on == 0);
    uint32_t len = (uint32_t)(LOG[1].t - LOG[0].t);
    assert(len == (uint32_t)((uint64_t)NUM * 5 / (100ull * DEN)) && len > 270 && len < 280);
}
static void arp_stops_when_empty(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 0, 0); arun(&a, &p, 1000, 1000);
    ain(&a, 0, 62, 100, 0); arun(&a, &p, 4000, 1000);          /* the note began at 0 and is still sounding at 1100 */
    int got[8], c = aons(got); assert(c == 1);
    assert(NLOG == 2 && LOG[1].e.on == 0 && LOG[1].t == 2757); /* gate ends: 5514 / 2 */
    assert(arp_sounding(&a) == 0);
}
static void arp_latch(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50);
    ain(&a, 1, 60, 0, EV_LATCH); ain(&a, 1, 64, 0, EV_LATCH); arun(&a, &p, 1000, 1000);
    ain(&a, 0, 60, 0, 0); ain(&a, 0, 64, 0, 0); arun(&a, &p, 4 * 5515, 1000);
    static const int w[] = { 60, 64, 60, 64 }; expect_notes(w, 4);
    int n = NLOG; arp_unlatch(&a); arun(&a, &p, 2 * 5515, 1000);
    for (int i = n; i < NLOG; i++) assert(!LOG[i].e.on);        /* the sounding note finishes, nothing starts */
    check_paired();
}
static void arp_mode_change_no_stuck(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 90); hold3(&a);
    arun(&a, &p, 2 * 5515 + 2000, 1000);                         /* steps 0, 1, 2 played; the third sounds */
    int n = NLOG; p.mode = AM_OFF; arun(&a, &p, 1000, 1000);
    int ons = 0; for (int i = n; i < NLOG; i++) if (LOG[i].e.on) { ons++; assert(LOG[i].t == NOW - 1000); }
    assert(ons == 3);
    n = NLOG; ain(&a, 1, 72, 500, 0); arun(&a, &p, 1000, 1000);   /* direct while OFF */
    assert(NLOG == n + 1 && LOG[n].e.on && LOG[n].e.note == 72 && LOG[n].t == NOW - 1000 + 500);
    n = NLOG; ain(&a, 0, 72, 0, 0); p.mode = AM_UP; arun(&a, &p, 1000, 1000);   /* back: the keys are cut, then UP from 0 */
    int offs = 0; for (int i = n; i < NLOG; i++) if (!LOG[i].e.on) { offs++; assert(LOG[i].t == NOW - 1000); }
    assert(offs == 4 && LOG[NLOG - 1].e.on && LOG[NLOG - 1].e.note == 60 && LOG[NLOG - 1].t == NOW - 1000);
    /* release everything: nothing is left on */
    ain(&a, 0, 60, 0, 0); ain(&a, 0, 64, 0, 0); ain(&a, 0, 67, 0, 0); arun(&a, &p, 6000, 1000);
    check_paired();
}
static void arp_passthrough_keeps_legato(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_OFF, 1, 50);
    q_event e = { 10, 0, 64, 90, 1, 3, 4, EV_LEGATO | EV_ACCENT, 60 }; arp_input(&a, &e);
    q_event o[ARP_OUT]; int n = arp_block(&a, &p, NUM, DEN, 100, o);
    assert(n == 1 && o[0].offset == 10 && o[0].note == 64 && o[0].from == 60 && o[0].flags == (EV_LEGATO | EV_ACCENT) && o[0].step == 4);
}
static void arp_tempo_change(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 0, 0);
    uint32_t n1 = NUM, d1 = DEN;
    arun(&a, &p, 2000, 100);                                       /* step 0 at 0, step 1 pending at 5514 */
    seq_step_ratio(60, DIV_16, &NUM, &DEN);
    arun(&a, &p, 40000, 100);
    int k = 0; uint64_t ts[8];
    for (int i = 0; i < NLOG; i++) if (LOG[i].e.on) ts[k++] = LOG[i].t;
    assert(k >= 4);
    assert(ts[1] - ts[0] == n1 / d1);                              /* the old tempo's step already planned */
    assert(ts[2] - ts[1] >= 2 * n1 / d1 - 1 && ts[2] - ts[1] <= 2 * n1 / d1 + 1);
    assert(ts[3] - ts[2] >= 2 * n1 / d1 - 1 && ts[3] - ts[2] <= 2 * n1 / d1 + 1);
    for (int i = 1; i < NLOG; i++) assert(!(LOG[i].e.on && LOG[i - 1].e.on && LOG[i].t / 100 == LOG[i - 1].t / 100));
}
static void arp_ninth_note_ignored(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50);
    for (int i = 0; i < 9; i++) ain(&a, 1, 50 + i, 0, 0);
    arun(&a, &p, 10 * 5515, 1000);
    int got[64], c = aons(got); assert(c >= 9);
    for (int i = 0; i < 9; i++) assert(got[i] == 50 + i % 8);
    ain(&a, 0, 58, 0, 0); arun(&a, &p, 1000, 1000);
}
static void arp_noteoff_survives_overflow(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 0, 0); arun(&a, &p, 100, 100);
    for (int i = 0; i < 40; i++) ain(&a, 1, 100 + i % 20, 10, 0);
    ain(&a, 0, 62, 50, 0);
    arun(&a, &p, 20000, 100);
    int on62 = 0;                                                  /* the key's note-off got through: 62 is not played again */
    for (int i = 0; i < NLOG; i++) on62 += LOG[i].e.note == 62 && LOG[i].e.on;
    assert(on62 == 1);
}
/* the queue holds nothing but note-offs: one more for a held key still ends that note */
static void arp_noteoff_survives_full_of_offs(void)
{
    q_arp a; asetup(&a, 120); q_arpp p = ap(AM_UP, 1, 50); ain(&a, 1, 62, 0, 0); arun(&a, &p, 100, 100);
    for (int i = 0; i < ARP_IN; i++) ain(&a, 0, 100 + i, 10, 0);
    ain(&a, 0, 62, 50, 0);
    arun(&a, &p, 20000, 100);
    int on62 = 0;
    for (int i = 0; i < NLOG; i++) on62 += LOG[i].e.note == 62 && LOG[i].e.on;
    assert(on62 == 1);
}
/* 9 keys sounded directly in OFF, only 8 tracked: after the switch to UP the 9th key's note-off still comes out */
static void arp_untracked_noteoff_passes(void)
{
    q_arp a; asetup(&a, 120); q_arpp off = ap(AM_OFF, 1, 50), up = ap(AM_UP, 1, 50);
    for (int i = 0; i < 9; i++) ain(&a, 1, 50 + i, 0, 0);
    arun(&a, &off, 100, 100);
    arun(&a, &up, 100, 100);
    ain(&a, 0, 58, 0, 0); arun(&a, &up, 100, 100);
    int offs = 0;
    for (int i = 0; i < NLOG; i++) offs += !LOG[i].e.on && LOG[i].e.note == 58;
    assert(offs == 1);
}

int main(void)
{
    adsr_default_is_gate(); adsr_times(); adsr_release_from_attack(); adsr_attack_from_current_level();
    filter_env_amount_and_accent(); lfo_shapes(); lfo_jumps_softened(); lfo_free_shared_phase(); lfo_trig_restarts(); lfo_phase_advances();
    lfo_sync_tempo_change_continuous(); lfo_level_depth(); lfo_pitch_range(); sh_changes_once_per_cycle();
    arp_up_two_octaves(); arp_down(); arp_updown_no_repeat(); arp_order_follows_arrival(); arp_rand_in_set();
    arp_single_note_repeats(); arp_starts_at_first_note(); arp_gate_min(); arp_stops_when_empty(); arp_latch();
    arp_mode_change_no_stuck(); arp_passthrough_keeps_legato(); arp_tempo_change(); arp_ninth_note_ignored();
    arp_noteoff_survives_overflow(); arp_untracked_noteoff_passes(); arp_noteoff_survives_full_of_offs();
    puts("mod tests ok");
    return 0;
}

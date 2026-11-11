#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "mod.c"

static q_vparam dflt(void) { return (q_vparam){ 0, 64, 127, 20, FT_LP, 127, 0, 0, LS_SIN, 0, 5, 0, LD_CUT }; }
static pq_mod blk(int v, const q_vparam *p, int tempo) { pq_mod m; mod_set(v, p, tempo); mod_voice_block(v, &m); return m; }
static float semis(float cutoff) { return 12 * log2f(cutoff * Q_SR / 440) + 69; }
static const float DT = MOD_BLK / Q_SR;

static void setup(void) { mod_init(); }

static void adsr_default_is_gate(void)
{
    setup(); q_vparam p = dflt(); mod_set(0, &p, 120);
    mod_note_on(0, 0);
    assert(blk(0, &p, 120).env == 1);
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
    a = blk(0, &p, 120);
    assert(fabsf(semis(a.cutoff) - (15.5f + 60 * 119.5f / 127) + 60) < 0.01f);   /* SAW at phase 0: -1 */
}

static void lfo_trig_restarts(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.ltrig = 1; p.ldepth = 63; p.lrate = 5;
    mod_set(0, &p, 120); mod_note_on(0, 0);
    float s60 = 15.5f + 60 * 119.5f / 127;
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 400 + 111 * k; i++) mod_clock(120, MOD_BLK), blk(0, &p, 120);
        assert(fabsf(semis(blk(0, &p, 120).cutoff) - s60) > 5);
        mod_set(0, &p, 120); mod_note_on(0, 0);
        assert(fabsf(semis(blk(0, &p, 120).cutoff) - s60) < 0.01f);   /* sin 0 */
    }
}

static void lfo_sync_tempo_change_continuous(void)
{
    setup(); q_vparam p = dflt(); p.cutoff = 60; p.lshape = LS_SAW; p.ldepth = 63; p.lrate = 5;
    float s60 = 15.5f + 60 * 119.5f / 127, prev = -1; int tempo = 120;
    for (int i = 0; i < 6000; i++) {
        if (i == 3000) tempo = 90;
        float ph = (semis(blk(0, &p, tempo).cutoff) - s60 + 60) / 120;
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
    int changes = 0; float prev = blk(0, &p, 120).cutoff;
    for (int i = 0; i < 3 * 1838 - 20; i++) { float c = blk(0, &p, 120).cutoff; if (c != prev) changes++; prev = c; }
    assert(changes == 2);
    /* FREE: voices of a track share the value */
    p.ltrig = 0; mod_set(0, &p, 120); mod_set(1, &p, 120); mod_clock(120, 30000);
    assert(blk(0, &p, 120).cutoff == blk(1, &p, 120).cutoff);
}

int main(void)
{
    adsr_default_is_gate(); adsr_times(); adsr_release_from_attack(); adsr_attack_from_current_level();
    filter_env_amount_and_accent(); lfo_shapes(); lfo_free_shared_phase(); lfo_trig_restarts();
    lfo_sync_tempo_change_continuous(); lfo_level_depth(); lfo_pitch_range(); sh_changes_once_per_cycle();
    puts("mod tests ok");
    return 0;
}

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qfx.h"
#include "project.h"

static const float kRate = 44117.647f;
static const unsigned kBlock = 256;

// A 220 Hz saw at 0.5 peak, n samples.
static void saw(float *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) b[i] = 0.5f * (2.0f * fmodf(i * 220.0f / kRate, 1.0f) - 1.0f);
}

static void off_is_identity(void)
{
    static float in[4096], out[4096];
    saw(in, 4096);
    for (int type = IT_OFF; type < IT_N; type++) {
        qfx_init();
        memcpy(out, in, sizeof(in));
        qfx_insert(0, type, 100, 100, 100, 0, out, 4096);
        assert(!memcmp(in, out, sizeof(in)));
    }
    memcpy(out, in, sizeof(in));
    qfx_insert(0, IT_OFF, 100, 100, 100, 127, out, 4096);
    assert(!memcmp(in, out, sizeof(in)));
}

static void each_type_changes_signal(void)
{
    static float in[8192], out[8192];
    saw(in, 8192);
    for (int type = IT_DRIVE; type < IT_N; type++) {
        qfx_init();
        memcpy(out, in, sizeof(in));
        for (unsigned i = 0; i < 8192; i += kBlock) qfx_insert(0, type, 100, 100, 100, 127, out + i, kBlock);
        double d = 0.0;
        for (unsigned i = 0; i < 8192; i++) d += (double)(out[i] - in[i]) * (out[i] - in[i]);
        d = sqrt(d / 8192);
        printf("  type %d: rms difference %.3f\n", type, d);
        assert(d > 0.01);
    }
}

static void inserts_stay_finite(void)
{
    static float b[kBlock];
    unsigned seed = 1;
    for (int type = IT_DRIVE; type < IT_N; type++) {
        for (int k = 0; k < 2; k++) {
            const int p = k ? 0 : 127;
            qfx_init();
            float peak = 0.0f;
            for (unsigned n = 0; n < 2 * (unsigned)kRate; n += kBlock) {
                for (unsigned i = 0; i < kBlock; i++) {
                    seed = seed * 1664525u + 1013904223u;
                    b[i] = (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
                }
                qfx_insert(1, type, p, p, p, 127, b, kBlock);
                for (unsigned i = 0; i < kBlock; i++) {
                    assert(isfinite(b[i]));
                    if (fabsf(b[i]) > peak) peak = fabsf(b[i]);
                }
            }
            printf("  type %d knobs %d: peak %.2f\n", type, p, peak);
            assert(peak <= 4.0f);
        }
    }
}

static void comb_tunes_to_note(void)
{
    static float b[4096];
    qfx_init();
    memset(b, 0, sizeof(b));
    b[0] = 1.0f;
    qfx_insert(0, IT_COMB, 57, 120, 0, 127, b, 4096);
    int best = 0;
    double bv = -1e30;
    for (int lag = 50; lag < 400; lag++) {
        double a = 0.0;
        for (int i = 0; i + lag < 4096; i++) a += (double)b[i] * b[i + lag];
        if (a > bv) { bv = a; best = lag; }
    }
    printf("  comb: autocorrelation peak at %d (expected %.1f)\n", best, kRate / 220.0f);
    assert(fabsf(best - kRate / 220.0f) <= 2.0f);
}

static void crush_bits(void)
{
    static float b[4096];
    qfx_init();
    for (unsigned i = 0; i < 4096; i++) b[i] = -1.0f + 2.0f * i / 4096;
    qfx_insert(0, IT_CRUSH, 127, 0, 0, 127, b, 4096);
    float seen[8];
    int n = 0;
    for (unsigned i = 0; i < 4096; i++) {
        int f = 0;
        for (int k = 0; k < n; k++) f |= seen[k] == b[i];
        if (!f) { assert(n < 8); seen[n++] = b[i]; }
    }
    printf("  crush: %d distinct values\n", n);
    assert(n <= 4);
}

static void type_switch_resets_state(void)
{
    static float b[kBlock];
    unsigned seed = 7;
    qfx_init();
    for (unsigned n = 0; n < (unsigned)kRate; n += kBlock) {
        for (unsigned i = 0; i < kBlock; i++) {
            seed = seed * 1664525u + 1013904223u;
            b[i] = (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
        }
        qfx_insert(2, IT_COMB, 57, 127, 100, 127, b, kBlock);
    }
    memset(b, 0, sizeof(b));
    qfx_insert(2, IT_CHORUS, 50, 50, 0, 127, b, kBlock);
    qfx_insert(2, IT_COMB, 57, 127, 100, 127, b, kBlock);
    for (unsigned i = 0; i < kBlock; i++) assert(b[i] == 0.0f);
}

// DRIVE at K1 0 stays close to unity for a small signal: it must not mute the track
static void drive_zero_not_silent(void)
{
    static float b[8832];
    const unsigned n = (unsigned)(kRate * 0.2f);
    double in = 0.0, out = 0.0;
    qfx_init();
    for (unsigned i = 0; i < n; i++) b[i] = 0.1f * sinf(6.2831853f * 220.0f * i / kRate);
    for (unsigned i = 0; i < n; i++) in += (double)b[i] * b[i];
    for (unsigned i = 0; i < n; i += kBlock) qfx_insert(0, IT_DRIVE, 0, 127, 0, 127, b + i, n - i < kBlock ? n - i : kBlock);
    for (unsigned i = 0; i < n; i++) out += (double)b[i] * b[i];
    const double db = 10.0 * log10(out / in);
    printf("  drive 0: %.2f dB\n", db);
    assert(fabs(db) <= 3.0);
}

// RMS of a sine at hz through the PHASER with DEPTH 0 (stages fixed at 200 Hz), FEEDBK 0, MIX 64
static double phaser_gain(float hz)
{
    static float b[8192];
    qfx_init();
    for (unsigned i = 0; i < 8192; i++) b[i] = 0.5f * sinf(6.2831853f * hz * i / kRate);
    for (unsigned i = 0; i < 8192; i += kBlock) qfx_insert(0, IT_PHASER, 0, 0, 0, 64, b + i, kBlock);
    double sq = 0.0;
    for (unsigned i = 4096; i < 8192; i++) sq += (double)b[i] * b[i];
    return sqrt(sq / 4096);
}

// Each all-pass is at -90 degrees at its tuning f0 (200 Hz): the 4 stages total -180 degrees, a notch
// against the dry, at tan(pi f / fs) = tan(pi f0 / fs) * tan(22.5 degrees)
static void phaser_tuned_low(void)
{
    const double w = tan(3.14159265358979 * 200.0 / kRate) * tan(3.14159265358979 / 8.0);
    const float notch = (float)(atan(w) * kRate / 3.14159265358979);
    const double lo = phaser_gain(notch), hi = phaser_gain(5000.0f);
    printf("  phaser: %.0f Hz notch %.4f, 5 kHz %.4f\n", notch, lo, hi);
    assert(lo < hi * 0.1);
}

// --- qfx_block: track mix, delay, reverb, ducking, compressor, limiter ---

static qfx_track tr[QFX_TRACKS];
static qfx_master ms;
static float bus[QFX_TRACKS][kBlock];
static float *const busp[QFX_TRACKS] = {bus[0], bus[1], bus[2], bus[3]};

// the project's defaults: insert OFF, MIX 127, no sends, PAN 0, LEVEL 100; DLY TIME 1/8 at 120 BPM
static void defaults(void)
{
    for (int t = 0; t < QFX_TRACKS; t++) {
        tr[t].type = IT_OFF;
        tr[t].p1 = tr[t].p2 = tr[t].p3 = 64;
        tr[t].mix = 127;
        tr[t].dsend = tr[t].rsend = 0;
        tr[t].level = 100;
        tr[t].duck = 0;
        tr[t].pan = 0;
    }
    ms.dly = 11029.0f;
    ms.dfb = 40;
    ms.rsize = 80;
    ms.rtone = 64;
    ms.comp = 0;
    ms.duck = 0;
    memset(bus, 0, sizeof(bus));
}

static float sine(float amp, float hz, unsigned i) { return amp * sinf(6.2831853f * hz * i / kRate); }

static void defaults_are_transparent(void)
{
    static float l[kBlock], r[kBlock];
    const float hz[4] = {110.0f, 220.0f, 330.0f, 440.0f};
    qfx_init();
    defaults();
    for (unsigned b = 0; b < 20; b++) {
        for (int t = 0; t < QFX_TRACKS; t++)
            for (unsigned i = 0; i < kBlock; i++) bus[t][i] = sine(0.2f, hz[t], b * kBlock + i);
        float sum[kBlock];
        for (unsigned i = 0; i < kBlock; i++) sum[i] = (bus[0][i] + bus[1][i] + bus[2][i] + bus[3][i]) * 100.0f / 127.0f;
        qfx_block(busp, tr, &ms, -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++) {
            assert(fabsf(l[i] - sum[i]) <= 1e-6f);
            assert(fabsf(r[i] - sum[i]) <= 1e-6f);
        }
    }
}

// L and R of a constant 0.5 on track 0 at the given PAN, divided by the dry level (LEVEL 127)
static void pan_gains(int pan, float *gl, float *gr)
{
    static float l[kBlock], r[kBlock];
    qfx_init();
    defaults();
    tr[0].level = 127;
    tr[0].pan = (int8_t)pan;
    for (unsigned i = 0; i < kBlock; i++) bus[0][i] = 0.5f;
    qfx_block(busp, tr, &ms, -1, l, r, kBlock);
    *gl = l[kBlock - 1] / 0.5f;
    *gr = r[kBlock - 1] / 0.5f;
}

static void pan_law(void)
{
    float gl, gr;
    pan_gains(0, &gl, &gr);
    assert(fabsf(gl - 1.0f) < 1e-6f && fabsf(gr - 1.0f) < 1e-6f);
    pan_gains(-64, &gl, &gr);
    assert(fabsf(gl - 1.0f) < 1e-6f && fabsf(gr) < 1e-6f);
    pan_gains(63, &gl, &gr);
    assert(fabsf(gl) < 1e-6f && fabsf(gr - 1.0f) < 1e-6f);
    pan_gains(32, &gl, &gr);
    printf("  pan 32: L %.3f R %.3f\n", gl, gr);
    assert(fabsf(gl - 0.49f) < 0.01f && fabsf(gr - 1.0f) < 1e-6f);
}

// the frame of the largest |x| in [from, to)
static unsigned peak_at(const float *x, unsigned from, unsigned to)
{
    unsigned best = from;
    for (unsigned i = from; i < to; i++)
        if (fabsf(x[i]) > fabsf(x[best])) best = i;
    return best;
}

static void delay_lands_on_division(void)
{
    enum { N = 90 * 256 };
    static float l[N], r[N];
    qfx_init();
    defaults();
    tr[0].dsend = 127;
    ms.dfb = 64;
    for (unsigned b = 0; b < N / kBlock; b++) {
        memset(bus, 0, sizeof(bus));
        if (b == 0) bus[0][0] = 1.0f;
        qfx_block(busp, tr, &ms, -1, l + b * kBlock, r + b * kBlock, kBlock);
    }
    const unsigned e1 = peak_at(l, 100, 16000), e2 = peak_at(r, 16000, N);
    printf("  delay: 1st echo L at %u (%.3f), R there %.3f; 2nd echo R at %u (%.3f)\n", e1, l[e1], r[e1], e2, r[e2]);
    assert(abs((int)e1 - 11029) <= 2 && fabsf(l[e1]) > 0.1f && fabsf(r[e1]) < 1e-3f);
    assert(abs((int)e2 - 22059) <= 2 && fabsf(r[e2]) > 0.05f);
}

static void delay_time_halves_to_fit(void)
{
    const float a = qfx_delay_fit(88235.0f), b = qfx_delay_fit(264706.0f);
    printf("  delay fit: %.2f %.2f\n", a, b);
    assert(fabsf(a - 22059.0f) < 1.0f);
    assert(fabsf(b - 16544.0f) < 1.0f);
    assert(qfx_delay_fit(11029.0f) == 11029.0f);
}

static void noise(unsigned *seed, float amp)
{
    for (int t = 0; t < QFX_TRACKS; t++)
        for (unsigned i = 0; i < kBlock; i++) {
            *seed = *seed * 1664525u + 1013904223u;
            bus[t][i] = amp * ((*seed >> 8) * (2.0f / 16777216.0f) - 1.0f);
        }
}

static void delay_time_change_stays_finite(void)
{
    static float l[kBlock], r[kBlock];
    unsigned seed = 3;
    qfx_init();
    defaults();
    for (int t = 0; t < QFX_TRACKS; t++) tr[t].dsend = 127;
    ms.dfb = 127;
    for (unsigned b = 0; b < 2 * (unsigned)kRate / kBlock; b++) {
        noise(&seed, 0.5f);
        ms.dly = (b & 1) ? 22000.0f : 2000.0f;
        qfx_block(busp, tr, &ms, -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++) assert(isfinite(l[i]) && isfinite(r[i]));
    }
}

// energy of L + R in [from, to) seconds after an impulse into the reverb at REV SIZE size
static double reverb_energy(int size, float from, float to)
{
    static float l[kBlock], r[kBlock];
    double e = 0.0;
    qfx_init();
    defaults();
    tr[0].rsend = 127;
    ms.rsize = (uint8_t)size;
    for (unsigned b = 0; b * kBlock < (unsigned)(to * kRate); b++) {
        memset(bus, 0, sizeof(bus));
        if (b == 0) bus[0][0] = 1.0f;
        qfx_block(busp, tr, &ms, -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++) {
            const unsigned f = b * kBlock + i;
            if (f >= (unsigned)(from * kRate) && f < (unsigned)(to * kRate)) e += (double)l[i] * l[i] + (double)r[i] * r[i];
        }
    }
    return e;
}

static void reverb_tail(void)
{
    const double tail = reverb_energy(127, 0.5f, 0.7f);
    const double big = reverb_energy(127, 1.0f, 1.2f), small = reverb_energy(0, 1.0f, 1.2f);
    printf("  reverb: tail %.3g, at 1 s size 127 %.3g size 0 %.3g\n", tail, big, small);
    assert(tail > 1e-6);
    assert(big > small * 4.0);
}

static void duck_dips_and_recovers(void)
{
    enum { N = 40 * 256 };
    static float l[N], r[N];
    qfx_init();
    defaults();
    tr[0].level = tr[1].level = 127;
    tr[0].pan = -64;   // track 0 on L only
    tr[0].duck = 1;
    tr[1].pan = 63;    // track 1 on R only
    ms.duck = 127;
    for (unsigned b = 0; b < N / kBlock; b++) {
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = bus[1][i] = 0.2f;
        qfx_block(busp, tr, &ms, b == 0 ? 0 : -1, l + b * kBlock, r + b * kBlock, kBlock);
    }
    const unsigned rec = (unsigned)(0.15f * kRate) + 2;
    const float d0 = 20.0f * log10f(l[0] / 0.2f), d1 = 20.0f * log10f(l[rec] / 0.2f);
    printf("  duck: %.2f dB at the note, %.2f dB after 150 ms\n", d0, d1);
    assert(fabsf(d0 + 24.0f) <= 0.5f);
    assert(fabsf(d1) <= 0.5f);
    for (unsigned i = 0; i < N; i++) assert(r[i] == 0.2f);
}

static void duck_zero_is_bypass(void)
{
    enum { N = 20 * 256 };
    static float l[2][N], r[2][N];
    for (int k = 0; k < 2; k++) {
        qfx_init();
        defaults();
        for (int t = 0; t < QFX_TRACKS; t++) tr[t].duck = 1, tr[t].dsend = tr[t].rsend = 64;
        for (unsigned b = 0; b < N / kBlock; b++) {
            for (int t = 0; t < QFX_TRACKS; t++)
                for (unsigned i = 0; i < kBlock; i++) bus[t][i] = sine(0.2f, 100.0f * (t + 1), b * kBlock + i);
            qfx_block(busp, tr, &ms, k ? 10 : -1, l[k] + b * kBlock, r[k] + b * kBlock, kBlock);
        }
    }
    assert(!memcmp(l[0], l[1], sizeof(l[0])) && !memcmp(r[0], r[1], sizeof(r[0])));
}

// track 0 alone at LEVEL 127, a 220 Hz sine of amplitude amp, COMP 127: out's peak and gain (dB, RMS)
// over [from, 0.5 s)
static void compress(float amp, float from, float *peak, float *db)
{
    static float l[kBlock], r[kBlock];
    double in = 0.0, out = 0.0;
    *peak = 0.0f;
    qfx_init();
    defaults();
    tr[0].level = 127;
    ms.comp = 127;
    for (unsigned b = 0; b * kBlock < (unsigned)(0.5f * kRate); b++) {
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = sine(amp, 220.0f, b * kBlock + i);
        float x[kBlock];
        memcpy(x, bus[0], sizeof(x));
        qfx_block(busp, tr, &ms, -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++)
            if (b * kBlock + i >= (unsigned)(from * kRate)) {
                in += (double)x[i] * x[i];
                out += (double)l[i] * l[i];
                if (fabsf(l[i]) > *peak) *peak = fabsf(l[i]);
            }
    }
    *db = (float)(10.0 * log10(out / in));
}

static void compressor_reduces_loud(void)
{
    float peak, db;
    compress(0.9f, 0.05f, &peak, &db);
    printf("  comp: 0.9 sine -> peak %.3f (%.2f dB)\n", peak, db);
    assert(peak < 0.9f);
    compress(0.01f, 0.05f, &peak, &db);
    printf("  comp: 0.01 sine -> %.2f dB\n", db);
    assert(fabsf(db - 11.0f) <= 1.0f);
}

static void master_extremes_bounded(void)
{
    static float l[kBlock], r[kBlock];
    unsigned seed = 11;
    qfx_init();
    defaults();
    for (int t = 0; t < QFX_TRACKS; t++) tr[t].dsend = tr[t].rsend = 127, tr[t].level = 127, tr[t].duck = t != 0;
    ms.dfb = ms.rsize = ms.comp = ms.duck = 127;
    float peak = 0.0f;
    for (unsigned b = 0; b < 10 * (unsigned)kRate / kBlock; b++) {
        noise(&seed, 1.0f);
        qfx_block(busp, tr, &ms, b % 20 == 0 ? 37 : -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++) {
            assert(isfinite(l[i]) && isfinite(r[i]));
            assert(fabsf(l[i]) <= 1.0f && fabsf(r[i]) <= 1.0f);
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
        }
    }
    printf("  master extremes: peak %.3f\n", peak);
}

int main(void)
{
    off_is_identity();
    each_type_changes_signal();
    inserts_stay_finite();
    comb_tunes_to_note();
    crush_bits();
    type_switch_resets_state();
    drive_zero_not_silent();
    phaser_tuned_low();
    defaults_are_transparent();
    pan_law();
    delay_lands_on_division();
    delay_time_halves_to_fit();
    delay_time_change_stays_finite();
    reverb_tail();
    duck_dips_and_recovers();
    duck_zero_is_bypass();
    compressor_reduces_loud();
    master_extremes_bounded();
    printf("qfx_test ok\n");
    return 0;
}

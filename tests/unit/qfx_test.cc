#include <assert.h>
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
    printf("qfx_test ok\n");
    return 0;
}

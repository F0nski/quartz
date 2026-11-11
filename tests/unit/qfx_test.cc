#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qfx.h"
#include "project.h"

static const float kRate = 44117.647f;
static const unsigned kBlock = 256;

// what audio.c does each block: the reverb (one block late) on the other core, then the rest
static void block(float *const bus[QFX_TRACKS], const qfx_track tr[QFX_TRACKS], const qfx_master *m, int duck_at,
                  float *l, float *r, unsigned n)
{
    qfx_reverb();
    qfx_block(bus, tr, m, duck_at, l, r, n);
}

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
    ms.punch = 0;
    ms.fpb = 22059.0f;   // 120 BPM
    ms.bpos = 0.0f;
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
        block(busp, tr, &ms, -1, l, r, kBlock);
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
    block(busp, tr, &ms, -1, l, r, kBlock);
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
        block(busp, tr, &ms, -1, l + b * kBlock, r + b * kBlock, kBlock);
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
        block(busp, tr, &ms, -1, l, r, kBlock);
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
        block(busp, tr, &ms, -1, l, r, kBlock);
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
        block(busp, tr, &ms, b == 0 ? 0 : -1, l + b * kBlock, r + b * kBlock, kBlock);
    }
    const unsigned rec = (unsigned)(0.15f * kRate) + 2;
    const float d0 = 20.0f * log10f(l[0] / 0.2f), d1 = 20.0f * log10f(l[rec] / 0.2f);
    printf("  duck: %.2f dB at the note, %.2f dB after 150 ms\n", d0, d1);
    assert(fabsf(d0 + 24.0f) <= 0.5f);
    assert(fabsf(d1) <= 0.5f);
    for (unsigned i = 0; i < N; i++) assert(r[i] == 0.2f);
}

// track 0, a KIT track (DUCK off), a 220 Hz sine at LEVEL 127 into REVERB rsend, DUCK depth: duck_at in block 8
static void duck_kit_run(int rsend, int depth, float *l)
{
    static float r[kBlock];
    qfx_init();
    defaults();
    tr[0].level = 127;
    tr[0].rsend = rsend;
    ms.duck = depth;
    for (unsigned b = 0; b < 16; b++) {
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = sine(0.2f, 220.0f, b * kBlock + i);
        block(busp, tr, &ms, b == 8 ? 0 : -1, l + b * kBlock, r, kBlock);
    }
}

static void duck_dips_returns(void)
{
    enum { N = 16 * 256, AT = 8 * 256, W = 441 };
    static float a[N], b[N], dry[N];
    duck_kit_run(0, 127, a);
    duck_kit_run(0, 0, dry);
    assert(!memcmp(a, dry, sizeof(a)));   // the KIT track's dry is not ducked
    duck_kit_run(127, 127, a);
    duck_kit_run(127, 0, b);
    assert(!memcmp(a, b, AT * sizeof(float)));
    double ea = 0, eb = 0;   // the return's energy over 10 ms from duck_at
    for (unsigned i = AT; i < AT + W; i++) {
        ea += (double)(a[i] - dry[i]) * (a[i] - dry[i]);
        eb += (double)(b[i] - dry[i]) * (b[i] - dry[i]);
    }
    printf("  duck: return %.2f dB at duck_at\n", 10.0 * log10(ea / eb));
    assert(eb > 1e-6 && ea < eb * 0.02);
}

// DUCK 127 dips, then 0 from the next block: the dip recovers at its rate, no jump
static void duck_off_mid_dip_recovers(void)
{
    enum { N = 40 * 256 };
    static float l[N], r[N];
    qfx_init();
    defaults();
    tr[0].level = 127;
    tr[0].pan = -64;   // track 0 on L only
    tr[0].duck = 1;
    for (unsigned b = 0; b < N / kBlock; b++) {
        ms.duck = b == 0 ? 127 : 0;
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = 0.2f;
        block(busp, tr, &ms, b == 0 ? 0 : -1, l + b * kBlock, r + b * kBlock, kBlock);
    }
    const unsigned rec = (unsigned)(0.15f * kRate) + 2;
    for (unsigned i = 1; i < N; i++) assert(l[i] >= l[i - 1] && 20.0f * log10f(l[i] / l[i - 1]) <= 1.0f);
    assert(l[kBlock] < 0.2f && l[rec] == 0.2f);
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
            block(busp, tr, &ms, k ? 10 : -1, l[k] + b * kBlock, r[k] + b * kBlock, kBlock);
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
        block(busp, tr, &ms, -1, l, r, kBlock);
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
        block(busp, tr, &ms, b % 20 == 0 ? 37 : -1, l, r, kBlock);
        for (unsigned i = 0; i < kBlock; i++) {
            assert(isfinite(l[i]) && isfinite(r[i]));
            assert(fabsf(l[i]) <= 1.0f && fabsf(r[i]) <= 1.0f);
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
        }
    }
    printf("  master extremes: peak %.3f\n", peak);
}

// --- punch-ins ---

// the source on track 0 for frame f
typedef float (*source)(unsigned f);
static float src_saw_noise(unsigned f)
{
    unsigned seed = f * 2654435761u;   // the same noise on every run
    seed ^= seed >> 13;
    seed *= 1664525u;
    return 0.4f * (2.0f * fmodf(f * 220.0f / kRate, 1.0f) - 1.0f) + 0.1f * ((seed >> 8) * (2.0f / 16777216.0f) - 1.0f);
}
static float src_sine100(unsigned f) { return sine(0.5f, 100.0f, f); }
static float src_dc(unsigned f) { (void)f; return 0.5f; }

// frames of L and R with punch-in mask held in blocks [on, off) (block indexes), source until frame stop;
// bpos runs from 0 at 120 BPM
static void run_punch(float *L, float *R, unsigned blocks, uint16_t mask, unsigned on, unsigned off, source src,
                      unsigned stop)
{
    for (unsigned b = 0; b < blocks; b++) {
        memset(bus, 0, sizeof(bus));
        for (unsigned i = 0; i < kBlock; i++) {
            const unsigned f = b * kBlock + i;
            bus[0][i] = f < stop ? src(f) : 0.0f;
        }
        ms.punch = b >= on && b < off ? mask : 0;
        ms.bpos = fmodf((float)(b * kBlock), 4.0f * ms.fpb);
        block(busp, tr, &ms, -1, L + b * kBlock, R + b * kBlock, kBlock);
    }
}

static const unsigned kPB = 160;   // blocks per punch test: 0.93 s
static float pl[kPB * kBlock], pr[kPB * kBlock], ql[kPB * kBlock], qr[kPB * kBlock];

static void no_punch_is_identity(void)
{
    qfx_init();
    defaults();
    run_punch(ql, qr, kPB, 0, 0, 0, src_saw_noise, ~0u);
    qfx_init();
    defaults();
    ms.fpb = 12345.0f;
    run_punch(pl, pr, kPB, 0, 0, 0, src_saw_noise, ~0u);
    assert(!memcmp(pl, ql, sizeof(pl)) && !memcmp(pr, qr, sizeof(pr)));
}

static double rms_diff(const float *a, const float *b, unsigned from, unsigned to)
{
    double e = 0.0;
    for (unsigned i = from; i < to; i++) e += (double)(a[i] - b[i]) * (a[i] - b[i]);
    return sqrt(e / (to - from));
}

static void each_punch_changes_signal(void)
{
    for (int k = 5; k <= 13; k++) {
        qfx_init();
        defaults();
        run_punch(ql, qr, kPB, 0, 0, 0, src_saw_noise, ~0u);
        qfx_init();
        defaults();
        run_punch(pl, pr, kPB, (uint16_t)(1u << k), 10, 96, src_saw_noise, ~0u);
        const double d = rms_diff(pl, ql, 10 * kBlock, kPB * kBlock) + rms_diff(pr, qr, 10 * kBlock, kPB * kBlock);
        printf("  punch %d: rms diff %.3f\n", k + 1, d);
        assert(d > 0.01);
        for (unsigned i = 0; i < kPB * kBlock; i++) assert(isfinite(pl[i]) && isfinite(pr[i]));
    }
}

static void punch_fades_in(void)
{
    for (int k = 5; k <= 13; k++) {
        qfx_init();
        defaults();
        run_punch(pl, pr, kPB, (uint16_t)(1u << k), 10, 60, src_sine100, ~0u);
        float jump = 0.0f;
        for (unsigned i = 1; i < kPB * kBlock; i++)
            jump = fmaxf(jump, fmaxf(fabsf(pl[i] - pl[i - 1]), fabsf(pr[i] - pr[i - 1])));
        printf("  punch %d: largest step %.3f\n", k + 1, jump);
        assert(jump <= 0.1f);   // DOWNSAMPLE's own 4 kHz steps on this sine reach 0.08
    }
}

static double rms(const float *a, unsigned from, unsigned to)
{
    double e = 0.0;
    for (unsigned i = from; i < to; i++) e += (double)a[i] * a[i];
    return sqrt(e / (to - from));
}

static void gate_on_grid(void)
{
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 10, 0, kPB, src_dc, ~0u);
    const unsigned q = 22059 / 2;   // a 1/16 on, a 1/16 off
    for (unsigned s = 1; s < 3; s++) {
        assert(rms(pl, s * q + 100, s * q + q / 2 - 100) > 0.1);
        assert(rms(pl, s * q + q / 2 + 100, s * q + q - 100) < 1e-4);
    }
}

static void delay_throw_tails(void)
{
    qfx_init();
    defaults();
    ms.dfb = 0;
    const unsigned stop = 43 * kBlock;   // 0.25 s
    run_punch(pl, pr, kPB, 1u << 12, 0, 43, src_saw_noise, stop);
    double e = 0.0;
    for (unsigned i = (unsigned)(0.3f * kRate); i < (unsigned)(0.45f * kRate); i++) e += (double)pl[i] * pl[i] + (double)pr[i] * pr[i];
    printf("  delay throw: tail energy %.3g\n", e);
    assert(e > 1.0);
}

static float peak(const float *l, const float *r, float from, float to)
{
    float p = 0.0f;
    for (unsigned i = (unsigned)(from * kRate); i < (unsigned)(to * kRate); i++) p = fmaxf(p, fmaxf(fabsf(l[i]), fabsf(r[i])));
    return p;
}

static void freeze_holds_tail(void)
{
    static float L[1100 * kBlock], R[1100 * kBlock];   // 6.4 s
    qfx_init();
    defaults();
    ms.rsize = 0;
    run_punch(L, R, 1100, 1u << 13, 0, 345, src_saw_noise, (unsigned)(0.2f * kRate));   // held 2 s
    const float early = peak(L, R, 0.3f, 0.4f), late = peak(L, R, 1.8f, 1.9f), gone = peak(L, R, 6.0f, 6.3f);
    printf("  freeze: %.3g at 0.3 s, %.3g at 1.8 s, %.3g at 6 s\n", early, late, gone);
    assert(early > 0.01f);
    assert(late > 0.5f * early);
    assert(gone < 1e-3f * early);
}

// noise low-passed at ~2 kHz: what the half-rate loop buffer keeps; the same on every run
static float src_noise(unsigned f)
{
    static float t[600 * 256];
    static int made;
    if (!made) {
        float y = 0.0f;
        for (unsigned i = 0; i < 600 * 256; i++) {
            unsigned seed = i * 2654435761u;
            seed ^= seed >> 13;
            seed *= 1664525u;
            y += 0.25f * (((seed >> 8) * (2.0f / 16777216.0f) - 1.0f) - y);
            t[i] = 2.0f * y;
        }
        made = 1;
    }
    return t[f % (600 * 256)];
}
static float src_sine1k(unsigned f) { return sine(0.5f, 1000.0f, f); }

// normalized correlation of a[i] and b[i] over n
static double corr(const float *a, const float *b, unsigned n)
{
    double ab = 0, aa = 0, bb = 0;
    for (unsigned i = 0; i < n; i++) ab += (double)a[i] * b[i], aa += (double)a[i] * a[i], bb += (double)b[i] * b[i];
    return ab / sqrt(aa * bb + 1e-30);
}

// the first lag in [lo, hi) at which x repeats itself over [from, to), 0 none
static unsigned period(const float *x, unsigned from, unsigned to, unsigned lo, unsigned hi)
{
    for (unsigned lag = lo; lag < hi && from + lag < to; lag++)
        if (corr(x + from, x + from + lag, to - from - lag) > 0.8) return lag;
    return 0;
}

static void loop_no_jump_on_press(void)
{
    qfx_init();
    defaults();
    run_punch(ql, qr, kPB, 0, 0, 0, src_saw_noise, ~0u);
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 2, 11, kPB, src_saw_noise, ~0u);   // LOOP 1/16 at frame 2816, slice [0, 5515)
    float err = 0.0f;
    for (unsigned i = 11 * kBlock; i < 5514; i++) err = fmaxf(err, fabsf(pl[i] - ql[i]));
    printf("  loop: before the slice's end, off by %.4f\n", err);
    assert(err < 0.02f);
}

static void loop_repeats_grid_slice(void)
{
    qfx_init();
    defaults();
    run_punch(ql, qr, kPB, 0, 0, 0, src_saw_noise, ~0u);
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 2, 11, kPB, src_saw_noise, ~0u);
    const double c1 = corr(pl + 5516, ql + 1, 2000), c2 = corr(pl + 2 * 5515 + 1, ql + 1, 2000);
    printf("  loop: repeats correlate %.3f, %.3f\n", c1, c2);
    assert(c1 > 0.95 && c2 > 0.95);
}

static void loop_halves_to_fit(void)
{
    static float L[400 * kBlock], R[400 * kBlock];
    qfx_init();
    defaults();
    ms.fpb = 66176.0f;   // 40 BPM: a 1/8 is 33088, halved twice to 8272
    run_punch(L, R, 400, 1u << 0, 0, 400, src_noise, ~0u);
    const unsigned p = period(L, 40000, 60000, 1000, 17000);
    printf("  loop 1/8 at 40 BPM: period %u\n", p);
    assert(p >= 8262 && p <= 8282);
}

static void roll_keeps_start(void)
{
    qfx_init();
    defaults();
    run_punch(ql, qr, kPB, 0, 0, 0, src_noise, ~0u);
    qfx_init();
    defaults();
    for (unsigned b = 0; b < kPB; b++) {   // LOOP 1/8 from block 0, LOOP 1/32 added at block 60
        memset(bus, 0, sizeof(bus));
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = src_noise(b * kBlock + i);
        ms.punch = b < 60 ? 1u : 9u;
        ms.bpos = (float)(b * kBlock);
        block(busp, tr, &ms, -1, pl + b * kBlock, pr + b * kBlock, kBlock);
    }
    const float len = 22059 / 8.0f;   // a repeat's start, from 0 on the 1/32 grid
    const unsigned at = (unsigned)(ceilf(61 * kBlock / len) * len + 0.5f);
    const double c = corr(pl + at + 2, ql + 2, 2000);
    printf("  roll: the 1/32 slice from the 1/8's start correlates %.3f\n", c);
    assert(c > 0.9);
}

static void priority(void)
{
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 0 | 1u << 4 | 1u << 14, 0, kPB, src_saw_noise, ~0u);
    assert(peak(pl, pr, (22059 + 512) / kRate, (kPB * kBlock - 1) / kRate) < 1e-4f);   // TAPE STOP: silent after a beat
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 0 | 1u << 14, 0, kPB, src_noise, ~0u);
    const unsigned p = period(pl, 22059 + 512, 2 * 22059 - 512, 1000, 12000);   // STUTTER's second beat: a 1/16
    printf("  priority: STUTTER over LOOP 1/8, period %u\n", p);
    assert(p >= 5400 && p <= 5630);
}

static void stutter_accelerates(void)
{
    static float L[400 * kBlock], R[400 * kBlock];
    qfx_init();
    defaults();
    run_punch(L, R, 400, 1u << 14, 0, 400, src_noise, ~0u);
    const float want[4] = {22059 / 2.0f, 22059 / 4.0f, 22059 / 8.0f, 22059 / 16.0f};
    for (unsigned k = 0; k < 4; k++) {
        const unsigned from = k * 22059 + 512, to = (k + 1) * 22059 - 512;
        const unsigned p = period(L, from, to, 200, 12000);
        printf("  stutter: beat %u period %u (want %.0f)\n", k + 1, p, want[k]);
        assert(fabsf(p - want[k]) <= 0.02f * want[k]);
    }
}

static void stutter_gate_chops(void)
{
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 15, 0, kPB, src_saw_noise, ~0u);
    const float q = 22059 / 16.0f;
    for (unsigned j = 1; j < 20; j++) {
        assert(rms(pl, (unsigned)(j * q + 100), (unsigned)(j * q + q / 2 - 100)) > 0.01);
        assert(rms(pl, (unsigned)(j * q + q / 2 + 100), (unsigned)((j + 1) * q - 100)) < 1e-3);
    }
}

static void tape_stop_to_silence(void)
{
    qfx_init();
    defaults();
    run_punch(pl, pr, kPB, 1u << 4, 0, kPB, src_sine1k, ~0u);
    unsigned zc = 0;
    const unsigned at = (unsigned)(0.6f * 22059);
    for (unsigned i = at; i < at + 1000; i++) zc += (pl[i] < 0) != (pl[i + 1] < 0);
    const float hz = zc / 2.0f * kRate / 1000.0f;
    printf("  tape stop: %.0f Hz at 60 %%\n", hz);
    assert(hz < 500.0f && hz > 100.0f);
    assert(peak(pl, pr, (22059 + 512) / kRate, (kPB * kBlock - 1) / kRate) < 1e-4f);
}

static void noise_track0(unsigned b)
{
    memset(bus, 0, sizeof(bus));
    for (unsigned i = 0; i < kBlock; i++) bus[0][i] = src_noise(b * kBlock + i);
}

static void loop_survives_tempo_and_stop(void)
{
    qfx_init();
    defaults();
    float bp = 0.0f;
    for (unsigned b = 0; b < 600; b++) {
        noise_track0(b);
        ms.punch = 1u;
        ms.fpb = b < 400 ? ((b & 1) ? 66176.0f : 22059.0f) : 22059.0f;
        ms.bpos = b < 400 ? fmodf((float)(b * kBlock), 4.0f * ms.fpb) : bp;   // then the free phase
        if (b >= 400) bp = fmodf(bp + kBlock, 4.0f * ms.fpb);
        block(busp, tr, &ms, -1, pl, pr, kBlock);
        for (unsigned i = 0; i < kBlock; i++) assert(isfinite(pl[i]) && fabsf(pl[i]) <= 1.0f && fabsf(pr[i]) <= 1.0f);
    }
}

static void all_punches_bounded(void)
{
    unsigned seed = 5;
    qfx_init();
    defaults();
    for (int t = 0; t < QFX_TRACKS; t++) tr[t].dsend = tr[t].rsend = 127, tr[t].level = 127;
    ms.dfb = ms.rsize = 127;
    ms.punch = 0xFFFF;
    for (unsigned b = 0; b < 10 * (unsigned)kRate / kBlock; b++) {
        noise(&seed, 1.0f);
        ms.bpos = fmodf((float)(b * kBlock), 4.0f * ms.fpb);
        block(busp, tr, &ms, -1, pl, pr, kBlock);
        for (unsigned i = 0; i < kBlock; i++) assert(isfinite(pl[i]) && fabsf(pl[i]) <= 1.0f && fabsf(pr[i]) <= 1.0f);
    }
}

static void punch_chatter(void)
{
    static float L[1100 * kBlock], R[1100 * kBlock];
    qfx_init();
    defaults();
    ms.rsize = 0;
    const unsigned stop = 345 * kBlock;   // 2 s
    for (unsigned b = 0; b < 1100; b++) {
        memset(bus, 0, sizeof(bus));
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = b * kBlock + i < stop ? src_saw_noise(b * kBlock + i) : 0.0f;
        ms.punch = b < 345 && (b & 1) ? (1u << 0 | 1u << 13) : 0;
        ms.bpos = fmodf((float)(b * kBlock), 4.0f * ms.fpb);
        block(busp, tr, &ms, -1, L + b * kBlock, R + b * kBlock, kBlock);
    }
    for (unsigned i = 0; i < 1100 * kBlock; i++) assert(isfinite(L[i]) && isfinite(R[i]));
    const float late = peak(L, R, 6.0f, 6.3f);
    printf("  chatter: %.3g at 6 s\n", late);
    assert(late < 1e-3f);
}

// the best correlation of x[0..n) with ref at a start in [lo, hi)
static double best_corr(const float *x, const float *ref, unsigned n, unsigned lo, unsigned hi)
{
    double b = 0.0;
    for (unsigned s = lo; s < hi; s++) b = fmax(b, corr(x, ref + s, n));
    return b;
}

// LOOP 1/8 held past the buffer, let go a block, pressed again in the same 1/8: it repeats recent sound only
static void loop_retap_after_long_hold(void)
{
    qfx_init();
    defaults();
    run_punch(ql, qr, kPB, 0, 0, 0, src_noise, ~0u);
    qfx_init();
    defaults();
    for (unsigned b = 0; b < kPB; b++) {
        memset(bus, 0, sizeof(bus));
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = src_noise(b * kBlock + i);
        ms.punch = b < 100 || b >= 102 ? 1u : 0u;
        ms.bpos = (float)(b * kBlock);
        block(busp, tr, &ms, -1, pl + b * kBlock, pr + b * kBlock, kBlock);
    }
    const double c = best_corr(pl + 33200, ql, 2000, 24000, 36000);
    printf("  loop retap: best correlation with the recent sound %.3f\n", c);
    assert(c > 0.9);
}

// a LOOP held for minutes keeps repeating the same slice exactly
static void loop_long_hold_precise(void)
{
    static float first[11029], L[kBlock], R[kBlock];
    const unsigned len = 11029, blocks = 7u * 60u * 44118u / kBlock;   // 7 min: past 2^24 frames
    float worst = 0.0f;
    qfx_init();
    defaults();
    ms.fpb = 22058.0f;   // a whole 1/8
    ms.punch = 1u;
    for (unsigned b = 0; b < blocks; b++) {
        memset(bus, 0, sizeof(bus));
        for (unsigned i = 0; i < kBlock; i++) bus[0][i] = src_sine1k(b * kBlock + i);
        ms.bpos = fmodf((float)(b * kBlock), 4.0f * ms.fpb);
        block(busp, tr, &ms, -1, L, R, kBlock);
        for (unsigned i = 0; i < kBlock; i++) {
            const unsigned f = b * kBlock + i;
            if (f >= len && f < 2 * len) first[f - len] = L[i];
            else if (f >= 2 * len && b > blocks - 100) worst = fmaxf(worst, fabsf(L[i] - first[f % len]));
        }
    }
    printf("  loop held 7 min: off by %.5f\n", worst);
    assert(worst < 1e-3f);
}

// the tempo moving under a held LOOP or TAPE STOP changes nothing: their lengths are taken at the press
static void tempo_frozen_while_held(void)
{
    const uint16_t masks[2] = {1u, 1u << 4};
    for (int k = 0; k < 2; k++) {
        qfx_init();
        defaults();
        run_punch(ql, qr, kPB, masks[k], 0, kPB, src_noise, ~0u);
        qfx_init();
        defaults();
        for (unsigned b = 0; b < kPB; b++) {
            noise_track0(b);
            ms.punch = masks[k];
            ms.bpos = (float)(b * kBlock);   // as in run_punch: the grid of the press
            ms.fpb = b < 20 ? 22059.0f : (b & 1) ? 44118.0f : 15000.0f;
            block(busp, tr, &ms, -1, pl + b * kBlock, pr + b * kBlock, kBlock);
        }
        ms.fpb = 22059.0f;
        float d = 0.0f;
        for (unsigned i = 0; i < kPB * kBlock; i++) d = fmaxf(d, fabsf(pl[i] - ql[i]));
        printf("  tempo moved under punch %d: off by %.5f\n", k ? 5 : 1, d);
        assert(d < 1e-6f);
    }
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
    duck_dips_returns();
    duck_zero_is_bypass();
    duck_off_mid_dip_recovers();
    compressor_reduces_loud();
    master_extremes_bounded();
    no_punch_is_identity();
    each_punch_changes_signal();
    punch_fades_in();
    gate_on_grid();
    delay_throw_tails();
    freeze_holds_tail();
    loop_no_jump_on_press();
    loop_repeats_grid_slice();
    loop_halves_to_fit();
    roll_keeps_start();
    priority();
    stutter_accelerates();
    stutter_gate_chops();
    tape_stop_to_silence();
    loop_survives_tempo_and_stop();
    all_punches_bounded();
    punch_chatter();
    loop_retap_after_long_hold();
    loop_long_hold_precise();
    tempo_frozen_while_held();
    printf("qfx_test ok\n");
    return 0;
}

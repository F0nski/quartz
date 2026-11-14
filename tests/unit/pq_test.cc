#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pq.h"
#include "mod.h"
#include "project.h"
#include "stmlib/utils/random.h"

static const float kRate = 44117.647f;
static const unsigned kBlock = 256;
static float buf[kBlock];

// What audio.c does around the voice: the ADSR and LFO (mod.c, defaults after mod_init) beside the Plaits voice.
static void reset(void) { mod_init(); pq_init(); }
static void note_on(int v, float note, float vel, int accent = 0) { mod_note_on(v, accent); pq_note_on(v, note, vel); }
static void note_off(int v) { mod_note_off(v); pq_note_off(v); }

// Renders n samples of voice v in platform-sized blocks; returns the peak, calls fn on each block.
static float render(int v, unsigned n, void (*fn)(const float *, unsigned) = 0)
{
    float peak = 0.0f;
    while (n) {
        unsigned k = n < kBlock ? n : kBlock;
        memset(buf, 0, sizeof(buf));
        pq_render(v, buf, k);
        for (unsigned i = 0; i < k; i++) {
            assert(isfinite(buf[i]));
            if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]);
        }
        if (fn) fn(buf, k);
        n -= k;
    }
    return peak;
}

static int crossings;
static float last;
static void count_crossings(const float *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (last < 0.0f && b[i] >= 0.0f) crossings++;
        last = b[i];
    }
}

static void pitch_is_right(void)
{
    reset();
    pq_set_engine(0, 8);
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);
    note_on(0, 69.0f, 1.0f);
    crossings = 0;
    last = 0.0f;
    render(0, (unsigned)kRate, count_crossings);
    printf("  pitch: %d Hz\n", crossings);
    assert(crossings > 440 * 0.99f && crossings < 440 * 1.01f);
}

static void all_models_sound(void)
{
    for (int m = 0; m < 24; m++) {
        reset();      // a fresh voice per model: no envelope or trigger left from the previous one
        pq_set_engine(1, m);
        pq_set_patch(1, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
        note_on(1, 60.0f, 1.0f);
        float peak = render(1, (unsigned)(kRate * 0.2f));
        printf("  model %2d peak %.3f\n", m, peak);
        assert(peak > 0.01f);
    }
}

static void engine_switch_while_sounding(void)
{
    reset();
    pq_set_engine(2, 8);
    pq_set_patch(2, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);
    note_on(2, 60.0f, 1.0f);
    render(2, (unsigned)(kRate * 0.1f));
    pq_set_engine(2, 17);
    float peak = render(2, (unsigned)(kRate * 0.2f));
    assert(peak < 4.0f);
    assert(peak > 0.01f);       // still sounding: the switch keeps the envelope and the gate
}

// Crossings per second over the next seconds of voice v.
static float frequency(int v, float seconds)
{
    crossings = 0;
    last = 0.0f;
    render(v, (unsigned)(kRate * seconds), count_crossings);
    return crossings / seconds;
}

static void glide_slides_between_notes(void)
{
    reset();
    pq_set_engine(5, 8);
    pq_set_patch(5, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);
    note_on(5, 48.0f, 1.0f);
    render(5, (unsigned)(kRate * 0.1f));
    float from = frequency(5, 0.1f);                 // 130.8 Hz
    pq_set_patch(5, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f);   // glide 1: time constant of 0.5 s
    note_on(5, 72.0f, 1.0f);
    float mid = frequency(5, 0.2f);                  // 523.3 Hz at the end, about 4 to 10 semitones up on the way
    printf("  glide: %.0f Hz, then %.0f Hz\n", from, mid);
    assert(from > 125.0f && from < 137.0f);
    assert(mid > from * 1.2f && mid < 523.0f * 0.8f);

    pq_set_patch(5, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);   // glide 0: the new note at once
    note_on(5, 48.0f, 1.0f);
    render(5, (unsigned)(kRate * 0.05f));
    float now = frequency(5, 0.2f);
    assert(now > 125.0f && now < 137.0f);
}

static double sum_sq;
static void add_squares(const float *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) sum_sq += (double)b[i] * b[i];
}

static float rms(int v, unsigned n)
{
    sum_sq = 0.0;
    render(v, n, add_squares);
    return (float)sqrt(sum_sq / n);
}

// glide_to: a new pitch on the held note, no trigger. Glide 1 is the project's glide byte 1 (1/127): with the
// full glide (a 0.5 s time constant) a note is not within 1 % after 0.6 s.
static void glide_to_keeps_envelope(void)
{
    const unsigned ms20 = (unsigned)(kRate * 0.02f);
    reset();
    pq_set_engine(0, 8);
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 0.1f, 1.0f / 127.0f);
    note_on(0, 48.0f, 1.0f);
    render(0, (unsigned)(kRate * 0.5f));
    float before = rms(0, ms20);
    pq_glide_to(0, 60.0f);
    float after = rms(0, ms20);
    render(0, (unsigned)(kRate * 0.58f));       // 0.6 s after glide_to: on the new note from there on
    float f = frequency(0, 0.5f);
    printf("  glide_to: rms %.4f then %.4f, then %.1f Hz\n", before, after, f);
    assert(before > 0.01f && after <= 1.2f * before);
    assert(f > 261.63f * 0.99f && f < 261.63f * 1.01f);

    // the VA's level follows the gate, a retrigger would not show: a bass drum's decay would start over
    pq_set_engine(4, 21);
    pq_set_patch(4, 0.5f, 0.5f, 0.5f, 0.1f, 1.0f / 127.0f);
    note_on(4, 48.0f, 1.0f);
    render(4, (unsigned)(kRate * 0.5f));
    before = rms(4, ms20);
    pq_glide_to(4, 60.0f);
    after = rms(4, ms20);
    printf("  glide_to, bass drum: rms %.5f then %.5f\n", before, after);
    assert(after <= 1.2f * before);
}

static void release_goes_silent(void)
{
    reset();
    pq_set_engine(3, 8);
    pq_set_patch(3, 0.5f, 0.5f, 0.5f, 0.3f, 0.0f);
    note_on(3, 60.0f, 1.0f);
    render(3, (unsigned)(kRate * 0.1f));
    assert(!pq_silent(3));
    note_off(3);
    unsigned n = 0;
    while (!pq_silent(3) && n < 5 * (unsigned)kRate) {
        render(3, kBlock);
        n += kBlock;
    }
    printf("  silent after %.2f s\n", n / kRate);
    assert(pq_silent(3));
}

static void off_on_between_renders_retriggers(void)
{
    reset();
    pq_set_engine(4, 21);
    pq_set_patch(4, 0.5f, 0.5f, 0.5f, 0.3f, 0.0f);
    note_on(4, 48.0f, 1.0f);
    float first = render(4, (unsigned)(kRate * 0.1f));
    render(4, (unsigned)(kRate * 0.9f));
    float tail = render(4, (unsigned)(kRate * 0.1f));
    note_off(4);
    note_on(4, 48.0f, 1.0f);
    float again = render(4, (unsigned)(kRate * 0.1f));
    printf("  bass drum: first %.3f tail %.4f again %.3f\n", first, tail, again);
    assert(tail < first * 0.1f);
    assert(again > first * 0.5f);
}

// The voice as audio.c drives it: the project's defaults (ADSR 0/64/127/20, LP open) unless v is edited.
static q_vparam defaults(void)
{
    return (q_vparam){ 0, 64, 127, 20, FT_LP, 127, 0, 0, LS_SIN, 0, 5, 0, LD_CUT };
}

static void start(int v, int model, const q_vparam &p, float note, float vel, int accent = 0)
{
    reset();
    pq_set_engine(v, model);
    pq_set_patch(v, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
    mod_set(v, &p, 120);
    note_on(v, note, vel, accent);
}

static float highs;     // energy of the first difference: the high frequencies
static void add_diffs(const float *b, unsigned n)
{
    for (unsigned i = 1; i < n; i++) highs += (b[i] - b[i - 1]) * (b[i] - b[i - 1]);
}

// High-frequency energy and peak of a settled VA note 48 for the filter type t and CUTOFF c.
static void filtered(int t, int c, float *hi, float *peak)
{
    q_vparam p = defaults();
    p.ftype = t; p.cutoff = c;
    start(0, 8, p, 48.0f, 1.0f);
    render(0, (unsigned)(kRate * 0.2f));
    highs = 0.0f;
    *peak = render(0, (unsigned)(kRate * 0.3f), add_diffs);
    *hi = highs;
}

static void lowpass_darkens(void)
{
    float ho, po, hc, pc;
    filtered(FT_LP, 127, &ho, &po);
    filtered(FT_LP, 30, &hc, &pc);
    printf("  lowpass: high energy %.2f -> %.2f\n", ho, hc);
    assert(hc < ho * 0.1f);
}

static void highpass_thins(void)
{
    float ho, po, hc, pc;
    filtered(FT_LP, 127, &ho, &po);
    filtered(FT_HP, 90, &hc, &pc);
    printf("  highpass: peak %.3f -> %.3f\n", po, pc);
    assert(pc < po * 0.5f);
}

static void release_holds_voice(void)
{
    q_vparam p = defaults();
    p.rel = 100;                                    // 0.001 * 10000^(100/127) s = 1.4 s
    start(0, 8, p, 60.0f, 1.0f);
    render(0, (unsigned)(kRate * 0.2f));
    note_off(0);
    for (unsigned n = 0; n < (unsigned)kRate; n += kBlock) {
        mod_set(0, &p, 120);
        render(0, kBlock);
        assert(!pq_silent(0));
    }
    float tail = render(0, kBlock);
    assert(tail > 0.01f);                           // still sounding: the release, not the gate, shuts it
}

static void enveloped_ignores_adsr_level(void)
{
    q_vparam p = defaults();
    p.atk = 127;                                    // 10 s: the VA would stay mute
    start(0, 20, p, 48.0f, 1.0f);
    float peak = render(0, (unsigned)(kRate * 0.01f));
    printf("  modal with ATTACK 127: peak %.3f in 10 ms\n", peak);
    assert(peak > 0.01f);
    start(1, 8, p, 48.0f, 1.0f);
    assert(render(1, (unsigned)(kRate * 0.01f)) < 0.01f);
}

static void filter_extremes_stay_finite(void)
{
    q_vparam p = defaults();
    p.res = 127; p.cutoff = 127; p.envamt = 63; p.ldepth = 63; p.ldest = LD_CUT; p.lrate = 70;
    start(0, 8, p, 36.0f, 1.0f, 1);
    float peak = 0.0f;
    for (unsigned n = 0; n < (unsigned)(kRate * 2.0f); n += kBlock) {
        mod_set(0, &p, 120);
        float pk = render(0, kBlock);               // render asserts every sample is finite
        if (pk > peak) peak = pk;
    }
    printf("  extremes: peak %.3f\n", peak);
    assert(peak <= 1.0f);
}

static const float kOldPeak = 0.6578f;     // the VA at defaults before the per-voice filter and ADSR (374914a)

static void defaults_match_old_level(void)
{
    q_vparam p = defaults();
    start(0, 8, p, 60.0f, 1.0f);
    float peak = 0.0f;
    for (unsigned n = 0; n < (unsigned)(kRate * 0.3f); n += kBlock) {
        mod_set(0, &p, 120);
        float pk = render(0, kBlock);
        if (pk > peak) peak = pk;
    }
    printf("  defaults: peak %.4f (old %.4f)\n", peak, kOldPeak);
    assert(peak > kOldPeak * 0.9f && peak < kOldPeak * 1.1f);
}

static void voice_bytes_reported(void)
{
    printf("  pq_voice_bytes = %u\n", pq_voice_bytes());
    assert(pq_voice_bytes() <= 24576);
}

// BD (engine 21): CHAR 0 is the sound of old, CHAR 1 the engine's aux, another sound that still plays
static float e_sum;
static void sum_energy(const float *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) e_sum += b[i] * b[i];
}
static float hit(float chr, bool set, float *energy)
{
    reset();
    stmlib::Random::Seed(1);              // the drums' noise: the same hit each time
    pq_set_engine(0, 21);
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
    if (set) pq_set_char(0, chr);
    note_on(0, 48.0f, 1.0f);
    e_sum = 0.0f;
    float peak = render(0, 4410, sum_energy);   // 100 ms
    *energy = e_sum;
    return peak;
}

static void char_mixes_out_and_aux(void)
{
    float e_old, e0, e1;
    float p_old = hit(0.0f, false, &e_old), p0 = hit(0.0f, true, &e0), p1 = hit(1.0f, true, &e1);
    printf("  char: energy %.2f at 0, %.2f at 1\n", e0, e1);
    assert(p0 == p_old && e0 == e_old);
    assert(p1 > 0.05f);
    assert(fabsf(e1 - e0) > 0.01f * e0);
}

static void char_resets_on_engine_switch(void)
{
    float e0;
    hit(0.0f, true, &e0);
    reset();
    stmlib::Random::Seed(1);
    pq_set_engine(0, 21);
    pq_set_char(0, 1.0f);
    pq_set_engine(0, 21);                 // the switch forgets CHAR
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
    note_on(0, 48.0f, 1.0f);
    e_sum = 0.0f;
    render(0, 4410, sum_energy);
    assert(e_sum == e0);
}

int main(void)
{
    pitch_is_right();
    all_models_sound();
    engine_switch_while_sounding();
    glide_slides_between_notes();
    glide_to_keeps_envelope();
    release_goes_silent();
    off_on_between_renders_retriggers();
    lowpass_darkens();
    highpass_thins();
    release_holds_voice();
    enveloped_ignores_adsr_level();
    filter_extremes_stay_finite();
    defaults_match_old_level();
    voice_bytes_reported();
    char_mixes_out_and_aux();
    char_resets_on_engine_switch();
    printf("pq_test ok\n");
    return 0;
}

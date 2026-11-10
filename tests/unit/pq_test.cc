#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pq.h"

static const float kRate = 44117.647f;
static const unsigned kBlock = 256;
static float buf[kBlock];

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
    pq_init();
    pq_set_engine(0, 8);
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);
    pq_note_on(0, 69.0f, 1.0f);
    crossings = 0;
    last = 0.0f;
    render(0, (unsigned)kRate, count_crossings);
    printf("  pitch: %d Hz\n", crossings);
    assert(crossings > 440 * 0.99f && crossings < 440 * 1.01f);
}

static void all_models_sound(void)
{
    pq_init();
    for (int m = 0; m < 24; m++) {
        pq_set_engine(1, m);
        pq_set_patch(1, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
        pq_note_on(1, 60.0f, 1.0f);
        float peak = render(1, (unsigned)(kRate * 0.2f));
        printf("  model %2d peak %.3f\n", m, peak);
        assert(peak > 0.01f);
    }
}

static void engine_switch_while_sounding(void)
{
    pq_init();
    pq_set_engine(2, 8);
    pq_set_patch(2, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f);
    pq_note_on(2, 60.0f, 1.0f);
    render(2, (unsigned)(kRate * 0.1f));
    pq_set_engine(2, 17);
    float peak = render(2, (unsigned)(kRate * 0.2f));
    assert(peak < 4.0f);
}

static void release_goes_silent(void)
{
    pq_init();
    pq_set_engine(3, 8);
    pq_set_patch(3, 0.5f, 0.5f, 0.5f, 0.3f, 0.0f);
    pq_note_on(3, 60.0f, 1.0f);
    render(3, (unsigned)(kRate * 0.1f));
    assert(!pq_silent(3));
    pq_note_off(3);
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
    pq_init();
    pq_set_engine(4, 21);
    pq_set_patch(4, 0.5f, 0.5f, 0.5f, 0.3f, 0.0f);
    pq_note_on(4, 48.0f, 1.0f);
    float first = render(4, (unsigned)(kRate * 0.1f));
    render(4, (unsigned)(kRate * 0.9f));
    float tail = render(4, (unsigned)(kRate * 0.1f));
    pq_note_off(4);
    pq_note_on(4, 48.0f, 1.0f);
    float again = render(4, (unsigned)(kRate * 0.1f));
    printf("  bass drum: first %.3f tail %.4f again %.3f\n", first, tail, again);
    assert(tail < first * 0.1f);
    assert(again > first * 0.5f);
}

static void voice_bytes_reported(void)
{
    printf("  pq_voice_bytes = %u\n", pq_voice_bytes());
    assert(pq_voice_bytes() <= 24576);
}

int main(void)
{
    pitch_is_right();
    all_models_sound();
    engine_switch_while_sounding();
    release_goes_silent();
    off_on_between_renders_retriggers();
    voice_bytes_reported();
    printf("pq_test ok\n");
    return 0;
}

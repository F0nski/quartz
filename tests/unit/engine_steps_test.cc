#include <assert.h>
#include <stdio.h>
#include "stmlib/dsp/hysteresis_quantizer.h"

// The middles ui.c's knob_zone sets for the ENGINE knobs that pick: each must play its choice in Plaits whatever
// choice the quantizer held before (its hysteresis), as the engines set them up.
static const int HEX[32] = {2, 6, 10, 14, 18, 21, 25, 29, 33, 37, 41, 45, 49, 53, 56, 60, 64, 68, 72, 76, 80, 84,
                            88, 91, 95, 99, 103, 107, 111, 115, 119, 123};   // six_op_engine.cc
static const int CHORD[11] = {6, 17, 28, 40, 51, 62, 74, 85, 96, 108, 119};  // chord_bank.cc
static const int ARP[12] = {5, 16, 26, 37, 48, 58, 69, 79, 90, 101, 111, 122};  // chiptune_engine.cc
static const int WORDS[6] = {49, 62, 74, 87, 100, 113};  // speech_engine.cc

static float hex_in(int v) { return v / 127.0f * 1.02f; }
static float chord_in(int v) { return v / 127.0f * 1.02f; }
static float arp_in(int v) { return v / 127.0f; }
static float words_in(int v) { return (v / 127.0f * 6.0f - 2.0f) * 0.275f; }

static void check(const char *name, const int *mid, int n, float hysteresis, float (*in)(int))
{
    for (int p = 0; p < n; p++)
        for (int k = 0; k < n; k++) {
            stmlib::HysteresisQuantizer2 q;
            q.Init(n, hysteresis, false);
            q.Process(in(mid[p]));
            assert(q.quantized_value() == p);
            int got = q.Process(in(mid[k]));
            if (got != k) {
                printf("%s: middle %d gives %d after %d, not %d\n", name, mid[k], got, p, k);
                assert(0);
            }
        }
}

int main(void)
{
    check("hex", HEX, 32, 0.005f, hex_in);
    check("chord", CHORD, 11, 0.075f, chord_in);
    check("arp", ARP, 12, 0.075f, arp_in);
    check("words", WORDS, 6, 0.1f, words_in);
    assert(42 / 127.0f * 6.0f <= 2.0f);   // ORATOR: 42 is still the blend of the synths (LPC)
    assert(49 / 127.0f * 6.0f > 2.0f);    // 49 is a word bank
    printf("engine_steps ok\n");
    return 0;
}

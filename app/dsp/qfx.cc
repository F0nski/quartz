// The four per-track inserts. Each works on mono blocks in place, in chunks of kChunk so that the dry
// signal can be mixed back in after the effect. Settings are read once per call.

#include "qfx.h"

#include <math.h>
#include <new>

#include "plaits/dsp/fx/overdrive.h"
#include "plaits/dsp/fx/sample_rate_reducer.h"
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"
#include "project.h"

namespace {

const float kSr = plaits::kSampleRate;
const float kTwoPi = 6.2831853f;
const unsigned kChunk = 64;
const unsigned kDelayMask = 1023;   // the delay lines hold 1024 samples: 23 ms

float e(float a, float b, int x) { return a * powf(b / a, x / 127.0f); }
// one-pole low-pass coefficient for a cutoff in Hz
float lp_coef(float hz) { return 1.0f - expf(-kTwoPi * hz / kSr); }

struct Track {
  int type;
  float lp;            // TONE's and COMB's low-pass
  float phase;         // LFO or ring modulator, in cycles
  float ap[4];         // PHASER's all-pass states
  float last;          // PHASER's feedback sample
  unsigned wpos;       // delay line write position
  plaits::Overdrive od;
  plaits::SampleRateReducer srr;
};

// Raw storage built with placement new in qfx_init (no C++ static constructors on the platform).
alignas(Track) char storage[QFX_TRACKS][sizeof(Track)];
Track* tracks[QFX_TRACKS];
// CHORUS' and COMB's delay line, per track
float ibuf[QFX_TRACKS][kDelayMask + 1] __attribute__((section(".pool")));

void reset(int t, int type) {
  Track& s = *tracks[t];
  s.type = type;
  s.lp = s.phase = s.last = 0.0f;
  for (int i = 0; i < 4; ++i) s.ap[i] = 0.0f;
  s.wpos = 0;
  s.od.Init();
  s.srr.Init();
  for (unsigned i = 0; i <= kDelayMask; ++i) ibuf[t][i] = 0.0f;
}

// the line's sample d samples before the next write, linearly interpolated (d >= 1)
float tap(const float* line, unsigned wpos, float d) {
  const float r = static_cast<float>(wpos) - d + 1024.0f;
  const unsigned i = static_cast<unsigned>(r);
  const float f = r - static_cast<float>(i);
  const float a = line[i & kDelayMask], b = line[(i + 1) & kDelayMask];
  return a + (b - a) * f;
}

void tone(Track& s, int k, float* x, unsigned n) {
  const float a = lp_coef(e(500.0f, 20000.0f, k));
  for (unsigned i = 0; i < n; ++i) x[i] = s.lp += a * (x[i] - s.lp);
}

// Folds x * gain + bias with Plaits' lut_fold, which spans an input of +-1 around index 0.5 (the
// WAVESHAPING engine's gain never exceeds 0.49): the input is first reflected into +-1 so that
// the high gains fold more instead of leaving the table.
void fold(float gain, float bias, float* x, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    float u = fmodf(x[i] * gain + bias + 1.0f, 4.0f);
    if (u < 0.0f) u += 4.0f;
    u = u < 2.0f ? u - 1.0f : 3.0f - u;
    x[i] = stmlib::InterpolateHermite(plaits::lut_fold + 1, 0.5f + 0.49f * u, 512.0f);
  }
}

void crush(Track& s, int p1, int p2, float* x, unsigned n) {
  const float steps = static_cast<float>(1 << (15 - static_cast<int>(lroundf(14.0f * p1 / 127.0f))));
  for (unsigned i = 0; i < n; ++i) {
    float q = roundf(x[i] * steps);
    q = q < -steps ? -steps : q > steps - 1.0f ? steps - 1.0f : q;   // a signed integer of 'bits' bits
    x[i] = q / steps;
  }
  // rate 0 leaves the rate alone: the reducer's band-limiting would add values between the steps
  if (p2) s.srr.Process<false>(e(44100.0f, 500.0f, p2) / kSr, x, n);
}

void chorus(Track& s, int t, float inc, float depth_ms, float* x, unsigned n) {
  float* line = ibuf[t];
  for (unsigned i = 0; i < n; ++i) {
    s.phase += inc;
    if (s.phase >= 1.0f) s.phase -= 1.0f;
    line[s.wpos] = x[i];
    const float d = (10.0f + depth_ms * plaits::Sine(s.phase)) * 0.001f * kSr;
    x[i] = tap(line, s.wpos + 1, d);
    s.wpos = (s.wpos + 1) & kDelayMask;
  }
}

void phaser(Track& s, float inc, float octaves, float fb, float* x, unsigned n) {
  float a = 0.0f;
  for (unsigned i = 0; i < n; ++i) {
    s.phase += inc;
    if (s.phase >= 1.0f) s.phase -= 1.0f;
    if ((i & 3) == 0) {   // the sweep is slow: the coefficient moves every 4 samples
      const float f = 200.0f * exp2f(octaves * (0.5f + 0.5f * plaits::Sine(s.phase)));
      const float w = tanf(3.14159265f * f / kSr);
      a = (1.0f - w) / (1.0f + w);
    }
    float y = x[i] + fb * s.last;
    for (int k = 0; k < 4; ++k) {
      const float o = a * y + s.ap[k];
      s.ap[k] = y - a * o;
      y = o;
    }
    s.last = y;
    x[i] = y * (1.0f - fb);   // the wet follows the feedback's gain at the resonance
  }
}

void ring(Track& s, float inc, float* x, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    s.phase += inc;
    if (s.phase >= 1.0f) s.phase -= 1.0f;
    x[i] *= plaits::Sine(s.phase);
  }
}

void comb(Track& s, int t, float d, float fb, float a, float* x, unsigned n) {
  float* line = ibuf[t];
  for (unsigned i = 0; i < n; ++i) {
    s.lp += a * (tap(line, s.wpos, d) - s.lp);
    const float y = x[i] + fb * s.lp;
    line[s.wpos] = y;
    s.wpos = (s.wpos + 1) & kDelayMask;
    x[i] = y * (1.0f - fb);   // the wet follows the feedback's gain at the resonance
  }
}

}  // namespace

extern "C" void qfx_init(void) {
  for (int t = 0; t < QFX_TRACKS; ++t) {
    tracks[t] = new (storage[t]) Track();
    reset(t, IT_OFF);
  }
}

extern "C" void qfx_insert(int t, int type, int p1, int p2, int p3, int mix, float* buf, unsigned n) {
  Track& s = *tracks[t];
  if (type == IT_OFF || mix == 0) {   // the state would be stale when the insert comes back
    if (s.type != IT_OFF) reset(t, IT_OFF);
    return;
  }
  if (type != s.type) reset(t, type);

  const float m = mix / 127.0f;
  const float lfo = e(0.05f, 5.0f, p1) / kSr;
  const float overdrive = 0.5f + 0.5f * p1 / 127.0f;   // 0.5: Overdrive's near-unity point
  const float fold_gain = e(1.0f, 16.0f, p1), fold_bias = (p2 - 64) / 64.0f;
  const float chorus_depth = 8.0f * p2 / 127.0f;
  const float octaves = 5.0f * p2 / 127.0f, phaser_fb = 0.9f * p3 / 127.0f;
  const float ring_inc = e(20.0f, 5000.0f, p1) / kSr;
  const float comb_hz = 440.0f * exp2f((p1 < 29 ? 29 - 69 : p1 - 69) / 12.0f);
  const float comb_d = kSr / comb_hz, comb_fb = 0.95f * p2 / 127.0f, comb_a = lp_coef(e(20000.0f, 500.0f, p3));

  float dry[kChunk];
  for (unsigned o = 0; o < n; o += kChunk) {
    float* x = buf + o;
    const unsigned k = n - o < kChunk ? n - o : kChunk;
    for (unsigned i = 0; i < k; ++i) dry[i] = x[i];
    switch (type) {
      case IT_DRIVE: s.od.Process(overdrive, x, k); tone(s, p2, x, k); break;
      case IT_FOLD: fold(fold_gain, fold_bias, x, k); tone(s, p3, x, k); break;
      case IT_CRUSH: crush(s, p1, p2, x, k); break;
      case IT_CHORUS: chorus(s, t, lfo, chorus_depth, x, k); break;
      case IT_PHASER: phaser(s, lfo, octaves, phaser_fb, x, k); break;
      case IT_RING: ring(s, ring_inc, x, k); break;
      case IT_COMB: comb(s, t, comb_d, comb_fb, comb_a, x, k); break;
    }
    for (unsigned i = 0; i < k; ++i) x[i] = dry[i] * (1.0f - m) + x[i] * m;
  }
}

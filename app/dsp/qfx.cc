// The four per-track inserts. Each works on mono blocks in place, in chunks of kChunk so that the dry
// signal can be mixed back in after the effect. Settings are read once per call.
// Behind them, qfx_block mixes the tracks to stereo with their sends into a ping-pong delay and Clouds'
// reverb, then ducking, compressor and limiter.

#include "qfx.h"

#include <math.h>
#include <string.h>
#include <new>

#include "fastmath.h"   // libm is slow on the device

#include "clouds/dsp/fx/reverb.h"

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

float e(float a, float b, int x) { return a * fm_powf(b / a, x / 127.0f); }
// one-pole low-pass coefficient for a cutoff in Hz
float lp_coef(float hz) { return 1.0f - fm_expf(-kTwoPi * hz / kSr); }

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

// The delay runs at half rate: kDlyLen samples per channel hold 0.51 s.
const unsigned kDlyLen = 11264;
const float kDlyScale = 8192.0f;   // int16 over +-4: four tracks' sends at full level don't clip
const float kDb = 6.0206f;         // dB per octave of amplitude: dB = kDb * log2(gain)

// a track's mix gains, at the end of the last block: they ramp from there to this block's
struct Mix {
  float g, pl, pr, ds, rs, m;   // LEVEL, PAN's left and right, delay and reverb sends, insert MIX
};

struct Master {
  clouds::Reverb reverb;
  bool primed;      // a block was rendered since qfx_init: the ramps have a start
  Mix mix[QFX_TRACKS];
  float dly;        // the delay's time at the end of the last block, in half-rate samples
  unsigned dpos;    // next write in dbuf
  float dlp[2];     // feedback low-pass, per channel
  float dlast[2];   // last half-rate output, per channel, for the upsampling
  float duck;       // ducking gain
  float rise;       // its recovery per frame, set by the dip
  float env, comp;  // compressor's detector and gain
  float lim;        // limiter's gain
};

alignas(Master) char mstorage[sizeof(Master)];
Master* master;
int16_t dbuf[2][kDlyLen] __attribute__((section(".pool")));
uint16_t rbuf[16384] __attribute__((section(".pool")));   // the reverb's FxEngine<16384>
// qfx_block's scratch
float dry[QFX_BLOCK], dsum[QFX_BLOCK], dg[QFX_BLOCK];
clouds::FloatFrame fx[QFX_BLOCK];   // reverb sends, then the reverb's and the delay's returns

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
      const float f = 200.0f * fm_exp2f(octaves * (0.5f + 0.5f * plaits::Sine(s.phase)));
      const float w = fm_tanf(3.14159265f * f / kSr);
      a = (w - 1.0f) / (w + 1.0f);   // -90 degrees per stage at f
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

Mix targets(const qfx_track& tr) {
  Mix c;
  c.g = tr.level / 127.0f;
  c.pl = tr.pan > 0 ? 1.0f - tr.pan / 63.0f : 1.0f;
  c.pr = tr.pan < 0 ? 1.0f + tr.pan / 64.0f : 1.0f;
  c.ds = tr.dsend / 127.0f;
  c.rs = tr.rsend / 127.0f;
  c.m = tr.mix / 127.0f;
  return c;
}

// the int16 line's sample d samples before the write at wpos (1 <= d <= kDlyLen), linearly interpolated
float dtap(const int16_t* line, unsigned wpos, float d) {
  const float r = static_cast<float>(wpos + kDlyLen) - d;
  unsigned i = static_cast<unsigned>(r);
  const float f = r - static_cast<float>(i);
  if (i >= kDlyLen) i -= kDlyLen;
  const unsigned j = i + 1 == kDlyLen ? 0 : i + 1;
  return (line[i] + (line[j] - line[i]) * f) * (1.0f / kDlyScale);
}

int16_t dstore(float x) { return static_cast<int16_t>(stmlib::Clip16(static_cast<int32_t>(x * kDlyScale))); }

// track t's insert, LEVEL, sends and PAN, added to l, r, dsum and fx's reverb send
void track(int t, const qfx_track& tr, float* x, float* l, float* r, unsigned n) {
  Mix& p = master->mix[t];
  const Mix c = targets(tr);
  const float k = 1.0f / n;
  if (tr.type == IT_OFF || (tr.mix == 0 && p.m == 0.0f)) {
    qfx_insert(t, tr.type, tr.p1, tr.p2, tr.p3, 0, x, n);
  } else {   // MIX is ramped here, the insert renders wet only
    memcpy(dry, x, n * sizeof(float));
    qfx_insert(t, tr.type, tr.p1, tr.p2, tr.p3, 127, x, n);
    for (unsigned i = 0; i < n; ++i) x[i] = dry[i] + (x[i] - dry[i]) * (p.m + (c.m - p.m) * k * (i + 1));
  }
  for (unsigned i = 0; i < n; ++i) {
    const float u = k * (i + 1);
    float y = x[i] * (p.g + (c.g - p.g) * u);
    dsum[i] += y * (p.ds + (c.ds - p.ds) * u);
    fx[i].l += y * (p.rs + (c.rs - p.rs) * u);
    if (tr.duck) y *= dg[i];
    l[i] += y * (p.pl + (c.pl - p.pl) * u);
    r[i] += y * (p.pr + (c.pr - p.pr) * u);
  }
  p = c;
}

// the ducking gain per frame into dg: down to the depth at duck_at, then up linearly in dB over 150 ms. Depth 0
// starts no dip, a running one recovers on
void duck(int depth, int duck_at, unsigned n) {
  Master& M = *master;
  const float db = 24.0f * depth / 127.0f;
  for (unsigned i = 0; i < n; ++i) {
    if (depth && static_cast<int>(i) == duck_at) {
      M.duck = fm_exp2f(-db / kDb);
      M.rise = fm_exp2f(db / kDb / (0.15f * kSr));
    } else if (M.duck < 1.0f) {
      M.duck *= M.rise;
      if (M.duck > 1.0f) M.duck = 1.0f;
    }
    dg[i] = M.duck;
  }
}

// the ping-pong delay at half rate on dsum, its returns added to fx
void delay(const qfx_master& m, unsigned n) {
  Master& M = *master;
  const float d = qfx_delay_fit(m.dly) * 0.5f, fb = 0.95f * m.dfb / 127.0f;
  const float a = lp_coef(4000.0f * 2.0f);   // 4 kHz at half rate
  const unsigned h = n / 2;
  for (unsigned k = 0; k < h; ++k) {
    const float dk = M.dly + (d - M.dly) * (k + 1) / h;
    const float yl = dtap(dbuf[0], M.dpos, dk), yr = dtap(dbuf[1], M.dpos, dk);
    M.dlp[0] += a * (yl - M.dlp[0]);
    M.dlp[1] += a * (yr - M.dlp[1]);
    // the input goes left, the feedback crosses over
    dbuf[0][M.dpos] = dstore(0.5f * (dsum[2 * k] + dsum[2 * k + 1]) + fb * M.dlp[1]);
    dbuf[1][M.dpos] = dstore(fb * M.dlp[0]);
    M.dpos = M.dpos + 1 == kDlyLen ? 0 : M.dpos + 1;
    fx[2 * k].l += 0.5f * (M.dlast[0] + yl);
    fx[2 * k].r += 0.5f * (M.dlast[1] + yr);
    fx[2 * k + 1].l += yl;
    fx[2 * k + 1].r += yr;
    M.dlast[0] = yl;
    M.dlast[1] = yr;
  }
  M.dly = d;
  for (int c = 0; c < 2; ++c)   // the low-pass would decay into denormals in silence
    if (fabsf(M.dlp[c]) < 1e-10f) M.dlp[c] = 0.0f;
}

// peak stereo-linked compressor: attack 5 ms, release 100 ms, 4:1 above -30 * comp / 127 dB, with
// makeup gain. The gain is computed every kComp frames and ramped between.
void compress(int comp, float* l, float* r, unsigned n) {
  Master& M = *master;
  const unsigned kComp = 16;
  const float t = -30.0f * comp / 127.0f, makeup = 0.5f * -t * 0.75f;
  const float att = 1.0f / (0.005f * kSr), rel = 1.0f / (0.1f * kSr);
  for (unsigned o = 0; o < n; o += kComp) {
    const unsigned e = o + kComp < n ? o + kComp : n;
    for (unsigned i = o; i < e; ++i) {
      const float p = fmaxf(fabsf(l[i]), fabsf(r[i]));
      M.env += (p > M.env ? att : rel) * (p - M.env);
    }
    if (M.env < 1e-10f) M.env = 0.0f;   // fm_log2f(0) is -127: no reduction, as wanted
    const float over = kDb * fm_log2f(M.env) - t;
    const float g = fm_exp2f(((over > 0.0f ? -0.75f * over : 0.0f) + makeup) / kDb);
    const float step = (g - M.comp) / (e - o);
    for (unsigned i = o; i < e; ++i) {
      M.comp += step;
      l[i] *= M.comp;
      r[i] *= M.comp;
    }
    M.comp = g;
  }
}

// peak stereo-linked limiter at 1.0: immediate attack, release 50 ms; gain exactly 1 below the ceiling
void limit(float* l, float* r, unsigned n) {
  Master& M = *master;
  const float rel = 1.0f / (0.05f * kSr);
  for (unsigned i = 0; i < n; ++i) {
    M.lim += rel * (1.0f - M.lim);
    if (M.lim > 0.9999f) M.lim = 1.0f;
    const float p = fmaxf(fabsf(l[i]), fabsf(r[i]));
    if (p * M.lim > 1.0f) M.lim = 1.0f / p;
    // 1 / p can round a hair above the ceiling
    l[i] = fminf(fmaxf(l[i] * M.lim, -1.0f), 1.0f);
    r[i] = fminf(fmaxf(r[i] * M.lim, -1.0f), 1.0f);
  }
}

}  // namespace

extern "C" void qfx_init(void) {
  for (int t = 0; t < QFX_TRACKS; ++t) {
    tracks[t] = new (storage[t]) Track();
    reset(t, IT_OFF);
  }
  // Clouds' Reverb and Plaits' FxEngine leave some members to static zeroing: zeroed after the (empty)
  // constructors, which would let the compiler drop a memset done before them
  master = new (mstorage) Master();
  memset(static_cast<void*>(master), 0, sizeof(Master));
  master->duck = master->lim = 1.0f;
  memset(dbuf, 0, sizeof(dbuf));
  memset(rbuf, 0, sizeof(rbuf));
  master->reverb.Init(rbuf);
  master->reverb.set_amount(1.0f);
  master->reverb.set_input_gain(0.2f);
  master->reverb.set_diffusion(0.625f);
}

extern "C" float qfx_delay_fit(float frames) {
  while (frames > 2.0f * kDlyLen) frames *= 0.5f;
  return frames;
}

extern "C" void qfx_block(float* const bus[QFX_TRACKS], const qfx_track tr[QFX_TRACKS], const qfx_master* m,
                          int duck_at, float* l, float* r, unsigned n) {
  Master& M = *master;
  if (!M.primed) {   // no fade-in: the first block starts at its own settings
    for (int t = 0; t < QFX_TRACKS; ++t) M.mix[t] = targets(tr[t]);
    M.dly = qfx_delay_fit(m->dly) * 0.5f;
    M.comp = fm_exp2f(0.5f * 30.0f * m->comp / 127.0f * 0.75f / kDb);
    M.primed = true;
  }
  for (unsigned i = 0; i < n; ++i) {
    l[i] = r[i] = dsum[i] = 0.0f;
    fx[i].l = 0.0f;
  }
  duck(m->duck, duck_at, n);
  for (int t = 0; t < QFX_TRACKS; ++t) track(t, tr[t], bus[t], l, r, n);

  for (unsigned i = 0; i < n; ++i) fx[i].r = fx[i].l;   // the reverb's input is mono
  M.reverb.set_time(0.5f + 0.48f * m->rsize / 127.0f);
  M.reverb.set_lp(0.3f + 0.65f * m->rtone / 127.0f);
  M.reverb.Process(fx, n);
  delay(*m, n);
  for (unsigned i = 0; i < n; ++i) {
    l[i] += fx[i].l * dg[i];
    r[i] += fx[i].r * dg[i];
  }

  if (m->comp) compress(m->comp, l, r, n);
  else M.comp = 1.0f;
  limit(l, r, n);
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
  const float comb_hz = 440.0f * fm_exp2f((p1 < 29 ? 29 - 69 : p1 - 69) / 12.0f);
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

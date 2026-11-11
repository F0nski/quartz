// The four per-track inserts. Each works on mono blocks in place, in chunks of kChunk so that the dry
// signal can be mixed back in after the effect. Settings are read once per call.
// Behind them, qfx_block mixes the tracks to stereo with their sends into a ping-pong delay and Clouds'
// reverb, then ducking, the punch-ins, compressor and limiter.

#include "qfx.h"

#include <math.h>
#include <string.h>
#include <new>

#include "fastmath.h"   // libm is slow on the device, and even fabsf or fmaxf is a call there

#include "clouds/dsp/fx/reverb.h"

#include "plaits/dsp/fx/overdrive.h"
#include "plaits/dsp/fx/sample_rate_reducer.h"
#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"
#include "stmlib/dsp/filter.h"
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

// The loop buffer's reader: LOOP (kind 0..3), TAPE STOP, STUTTER or STUTTER GATE, -1 none
struct Reader {
  int kind;
  uint32_t g;   // a slice's start, in frames of Master::now
  float lag;    // TAPE STOP: how far behind the live signal it reads
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
  float rpeak;      // the reverb's output peak in its last run: under kQuiet, its tail is over
  unsigned rn;      // frames in rio
  uint8_t rsize, rtone;   // the reverb's settings for its next run
  bool frozen;      // REVERB FREEZE: the reverb's next run holds its tail
  // the punch-ins: bit k of held = white key k + 1, as in the last block
  uint16_t held;
  float w[16];          // each punch-in's fade gain at the end of the last block
  float since[16];      // frames since each punch-in was pressed, at the block's start
  stmlib::Svf flt[3][2];             // LOW-PASS, HIGH-PASS, SWEEP; per channel
  plaits::SampleRateReducer srr[2];  // DOWNSAMPLE
  uint32_t now;         // frames since qfx_init at the block's start: the loop buffer's clock
  Reader rd;            // the loop buffer's reader in the last block
};

enum { P_TAPE = 4, P_LP, P_HP, P_SWEEP, P_CRUSH, P_DOWN, P_GATE, P_PAN, P_THROW, P_FREEZE, P_STUT, P_SGATE };

alignas(Master) char mstorage[sizeof(Master)];
Master* master;
int16_t dbuf[2][kDlyLen] __attribute__((section(".pool")));
// the loop buffer: the mix at half rate, 0.37 s
const unsigned kLoopLen = 8192;
const float kLoopMax = 2.0f * kLoopLen - 2.0f;   // the longest slice, in frames: its interpolation stays inside
int16_t lbuf[2][kLoopLen] __attribute__((section(".pool")));
uint16_t rbuf[16384] __attribute__((section(".pool")));   // the reverb's FxEngine<16384>
// qfx_block's scratch; the punch-ins take dry and dsum for the dry L and R once the tracks and the delay are done
float dry[QFX_BLOCK], dsum[QFX_BLOCK], dg[QFX_BLOCK];
clouds::FloatFrame fx[QFX_BLOCK];   // reverb sends, then the reverb's and the delay's returns
clouds::FloatFrame rio[QFX_BLOCK];  // the last block's reverb sends, which qfx_reverb turns into returns in place

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
    float u = x[i] * gain + bias + 1.0f;
    u -= 4.0f * fm_floorf(u * 0.25f);   // into [0, 4)
    u = u < 2.0f ? u - 1.0f : 3.0f - u;
    x[i] = stmlib::InterpolateHermite(plaits::lut_fold + 1, 0.5f + 0.49f * u, 512.0f);
  }
}

void crush(Track& s, int p1, int p2, float* x, unsigned n) {
  const float steps = static_cast<float>(1 << (15 - static_cast<int>(14.0f * p1 / 127.0f + 0.5f)));
  for (unsigned i = 0; i < n; ++i) {
    float q = fm_floorf(x[i] * steps + 0.5f);
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
    if ((i & 15) == 0) {   // the sweep is slow (5 Hz at most): the coefficient moves every 16 samples
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

constexpr float kQuiet = 1e-5f;   // -100 dB

bool silent(const float* x, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    if (x[i] != 0.0f) return false;
  return true;
}

// track t's insert, LEVEL, sends and PAN, added to l, r, dsum and fx's reverb send
void track(int t, const qfx_track& tr, float* x, float* l, float* r, unsigned n) {
  Mix& p = master->mix[t];
  const Mix c = targets(tr);
  if (tr.type == IT_OFF && silent(x, n)) {   // nothing to add: most tracks, most of the time
    qfx_insert(t, IT_OFF, 0, 0, 0, 0, x, n);
    p = c;
    return;
  }
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
  const float d = qfx_delay_fit(m.dly) * 0.5f;
  float fb = 0.95f * m.dfb / 127.0f;
  if (m.punch >> P_THROW & 1) fb = fm_maxf(fb, 0.6f);   // DELAY THROW
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
    if (fm_fabsf(M.dlp[c]) < 1e-10f) M.dlp[c] = 0.0f;
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
      const float p = fm_maxf(fm_fabsf(l[i]), fm_fabsf(r[i]));
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
    const float p = fm_maxf(fm_fabsf(l[i]), fm_fabsf(r[i]));
    if (p * M.lim > 1.0f) M.lim = 1.0f / p;
    // 1 / p can round a hair above the ceiling
    l[i] = fm_minf(fm_maxf(l[i] * M.lim, -1.0f), 1.0f);
    r[i] = fm_minf(fm_maxf(r[i] * M.lim, -1.0f), 1.0f);
  }
}

// --- the punch-ins ---

// x mod q, for x >= 0
float wrap(float x, float q) { return x - q * fm_floorf(x / q); }

// a 1/16 gate's gain at x frames into a period of q: on for the first half, 2 ms ramps at both edges
float chop(float x, float q) {
  const float h = 0.5f * q, ramp = 0.002f * kSr;
  return x < h ? fm_minf(1.0f, x / ramp) : fm_maxf(0.0f, 1.0f - (x - h) / ramp);
}

// the effect of punch-in k on l and r (wet only)
void punch_fx(int k, const qfx_master& m, float* l, float* r, unsigned n) {
  Master& M = *master;
  switch (k) {
    case P_GATE: {
      const float q = 0.25f * m.fpb;
      for (unsigned i = 0; i < n; ++i) {
        const float g = chop(wrap(m.bpos + i, q), q);
        l[i] *= g;
        r[i] *= g;
      }
      break;
    }
    case P_CRUSH:
      for (unsigned i = 0; i < n; ++i) {
        l[i] = fm_floorf(l[i] * 32.0f + 0.5f) * (1.0f / 32.0f);
        r[i] = fm_floorf(r[i] * 32.0f + 0.5f) * (1.0f / 32.0f);
      }
      break;
    case P_DOWN:
      M.srr[0].Process<false>(4000.0f / kSr, l, n);
      M.srr[1].Process<false>(4000.0f / kSr, r, n);
      break;
    case P_LP:
    case P_HP: {
      stmlib::Svf* f = M.flt[k - P_LP];
      for (int c = 0; c < 2; ++c) {
        f[c].set_f_q<stmlib::FREQUENCY_FAST>((k == P_LP ? 400.0f : 1500.0f) / kSr, k == P_LP ? 1.2f : 0.8f);
        float* x = c ? r : l;
        for (unsigned i = 0; i < n; ++i)
          x[i] = k == P_LP ? f[c].Process<stmlib::FILTER_MODE_LOW_PASS>(x[i])
                           : f[c].Process<stmlib::FILTER_MODE_HIGH_PASS>(x[i]);
      }
      break;
    }
    case P_SWEEP: {   // 8 kHz down to 200 Hz and back, a bar from the press, exponential
      stmlib::Svf* f = M.flt[2];
      for (unsigned i = 0; i < n; ++i) {
        if ((i & 15) == 0) {
          const float u = wrap(M.since[P_SWEEP] + i, 4.0f * m.fpb) / (4.0f * m.fpb);
          const float hz = 200.0f * fm_exp2f(5.321928f * fm_fabsf(2.0f * u - 1.0f));   // 40 = 2^5.32
          f[0].set_f_q<stmlib::FREQUENCY_FAST>(hz / kSr, 6.0f);
          f[1].set_f_q<stmlib::FREQUENCY_FAST>(hz / kSr, 6.0f);
        }
        l[i] = f[0].Process<stmlib::FILTER_MODE_LOW_PASS>(l[i]);
        r[i] = f[1].Process<stmlib::FILTER_MODE_LOW_PASS>(r[i]);
      }
      break;
    }
    case P_PAN:   // a sine balance, a cycle per beat
      for (unsigned i = 0; i < n; ++i) {
        const float p = plaits::Sine(wrap(m.bpos + i, m.fpb) / m.fpb);
        l[i] *= p > 0.0f ? 1.0f - p : 1.0f;
        r[i] *= p < 0.0f ? 1.0f + p : 1.0f;
      }
      break;
  }
}


// --- the loop buffer ---

// the reader the punch-ins ask for: TAPE STOP, then STUTTER GATE, STUTTER, the shortest LOOP held
int reader_kind(uint16_t p) {
  if (p >> P_TAPE & 1) return P_TAPE;
  if (p >> P_SGATE & 1) return P_SGATE;
  if (p >> P_STUT & 1) return P_STUT;
  for (int k = 3; k >= 0; --k)
    if (p >> k & 1) return k;
  return -1;
}

bool slicing(int kind) { return kind >= 0 && kind != P_TAPE; }

float fit(float len) {
  while (len > kLoopMax) len *= 0.5f;
  return len;
}

// a slice reader's length this block
float slice_len(int kind, const qfx_master& m) {
  static const float kLoop[4] = {0.5f, 1.0f / 3.0f, 0.25f, 0.125f};   // 1/8, 1/12, 1/16, 1/32, in beats
  if (kind < 4) return fit(kLoop[kind] * m.fpb);
  if (kind == P_SGATE) return fit(m.fpb / 16.0f);
  const int beats = static_cast<int>(master->since[P_STUT] / m.fpb);   // STUTTER: 1/8, 1/16, 1/32, then 1/64
  return fit(0.5f * m.fpb / static_cast<float>(1 << (beats < 3 ? beats : 3)));
}

// the buffer's frame at full-rate position base + off (off >= 0, base + off already written), channel c
float lread(int c, uint32_t base, float off) {
  const float h = 0.5f * (static_cast<float>(base & 1u) + off);
  const unsigned i = static_cast<unsigned>(h);
  const float f = h - static_cast<float>(i);
  const unsigned a = ((base >> 1) + i) & (kLoopLen - 1), b = (a + 1) & (kLoopLen - 1);
  return (lbuf[c][a] + (lbuf[c][b] - lbuf[c][a]) * f) * (1.0f / kDlyScale);
}

// a reader's frame i of the block into *a, *b; live: the mix
void read(Reader& r, const qfx_master& m, float len, unsigned i, const float* l, const float* rr, float* a, float* b) {
  const uint32_t t = master->now + i;
  if (r.kind < 0) {
    *a = l[i];
    *b = rr[i];
  } else if (r.kind == P_TAPE) {   // 1 to 0 in a beat (0.7 s at most), silence then
    const float span = fm_minf(m.fpb, 0.7f * kSr), s = master->since[P_TAPE] + i;
    if (s >= span) {
      *a = *b = 0.0f;
      return;
    }
    r.lag += s / span;   // a frame of reading moves 1 - s / span frames
    const float back = r.lag + 2.0f;   // the last two frames' pair is the newest written
    const uint32_t whole = static_cast<uint32_t>(back) + 1u;
    *a = lread(0, t - whole, static_cast<float>(whole) - back);
    *b = lread(1, t - whole, static_cast<float>(whole) - back);
  } else {
    const float off = static_cast<float>(t - r.g);
    float g = 1.0f;
    if (r.kind == P_SGATE) g = chop(wrap(off, len), len);
    if (off < len) {   // the slice's first pass is the live signal
      *a = g * l[i];
      *b = g * rr[i];
    } else {
      const float o = wrap(off, len);
      *a = g * lread(0, r.g, o);
      *b = g * lread(1, r.g, o);
    }
  }
}

// LOOP, TAPE STOP and the STUTTERs on l and r: the mix is written into the buffer, then read back
void loop_buffer(const qfx_master& m, float* l, float* r, unsigned n) {
  Master& M = *master;
  Reader old = M.rd;
  const int kind = reader_kind(m.punch);
  if (kind != old.kind) {
    M.rd.kind = kind;
    M.rd.lag = 0.0f;
    // a slice reader after another keeps its start (a roll); else the last line of its grid
    if (slicing(kind) && !slicing(old.kind)) {
      const float grid = kind == P_STUT ? fit(0.5f * m.fpb) : slice_len(kind, m);
      M.rd.g = M.now - static_cast<uint32_t>(wrap(m.bpos, grid));
    }
  }
  // the writer stops once a slice's start would be overwritten
  for (unsigned i = 0; i < n; i += 2) {
    const uint32_t t = M.now + i;
    if ((slicing(old.kind) || slicing(kind)) && static_cast<float>(t - M.rd.g) >= kLoopMax) continue;
    const unsigned k = (t >> 1) & (kLoopLen - 1);
    lbuf[0][k] = dstore(0.5f * (l[i] + l[i + 1]));
    lbuf[1][k] = dstore(0.5f * (r[i] + r[i + 1]));
  }
  if (kind >= 0 || old.kind >= 0) {
    const float lo = old.kind >= 0 ? slice_len(old.kind, m) : 0.0f, ln = kind >= 0 ? slice_len(kind, m) : 0.0f;
    const bool fade = kind != old.kind;
    for (unsigned i = 0; i < n; ++i) {
      float a, b;
      read(M.rd, m, ln, i, l, r, &a, &b);
      if (fade) {   // from the old reader (or the live mix) to the new one over the block
        float c, d;
        read(old, m, lo, i, l, r, &c, &d);
        const float w = static_cast<float>(i + 1) / n;
        a = c + w * (a - c);
        b = d + w * (b - d);
      }
      l[i] = a;
      r[i] = b;
    }
  }
  M.now += n;
}

// the punch-ins on the mix, in their order; each fades in and out over a block
void punches(const qfx_master& m, float* l, float* r, unsigned n) {
  static const int kOrder[] = {P_GATE, P_CRUSH, P_DOWN, P_LP, P_HP, P_SWEEP, P_PAN};
  Master& M = *master;
  loop_buffer(m, l, r, n);
  for (int k : kOrder) {
    const float w0 = M.w[k], w1 = m.punch >> k & 1 ? 1.0f : 0.0f;
    if (w0 == 0.0f && w1 == 0.0f) continue;
    if (w0 == 0.0f) {   // pressed: from a clean state
      if (k >= P_LP && k <= P_SWEEP) M.flt[k - P_LP][0].Init(), M.flt[k - P_LP][1].Init();
      if (k == P_DOWN) M.srr[0].Init(), M.srr[1].Init();
    }
    memcpy(dry, l, n * sizeof(float));
    memcpy(dsum, r, n * sizeof(float));
    punch_fx(k, m, l, r, n);
    for (unsigned i = 0; i < n; ++i) {
      const float w = w0 + (w1 - w0) * (i + 1) / n;
      l[i] = dry[i] + w * (l[i] - dry[i]);
      r[i] = dsum[i] + w * (r[i] - dsum[i]);
    }
    M.w[k] = w1;
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
  memset(lbuf, 0, sizeof(lbuf));
  master->rd.kind = -1;
  memset(rbuf, 0, sizeof(rbuf));
  master->reverb.Init(rbuf);
  master->reverb.set_amount(1.0f);
  master->reverb.set_input_gain(0.2f);
  master->reverb.set_diffusion(0.625f);
  for (int k = 0; k < 3; ++k) master->flt[k][0].Init(), master->flt[k][1].Init();
  master->srr[0].Init();
  master->srr[1].Init();
}

extern "C" float qfx_delay_fit(float frames) {
  while (frames > 2.0f * kDlyLen) frames *= 0.5f;
  return frames;
}

extern "C" void qfx_reverb(void) {
  Master& M = *master;
  bool in = false;
  for (unsigned i = 0; i < M.rn; ++i) in |= rio[i].l != 0.0f;
  if (!in && M.rpeak < kQuiet) return;   // idle: what stays in its lines is under -100 dB, rio holds silence
  // REVERB FREEZE: no loss in the loop, the tail holds
  M.reverb.set_time(M.frozen ? 1.0f : 0.5f + 0.48f * M.rsize / 127.0f);
  M.reverb.set_lp(M.frozen ? 1.0f : 0.3f + 0.65f * M.rtone / 127.0f);
  M.reverb.Process(rio, M.rn);
  M.rpeak = 0.0f;
  for (unsigned i = 0; i < M.rn; ++i) M.rpeak = fm_maxf(M.rpeak, fm_maxf(fm_fabsf(rio[i].l), fm_fabsf(rio[i].r)));
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
  for (int k = 0; k < 16; ++k)
    if (m->punch >> k & 1 && !(M.held >> k & 1)) M.since[k] = 0.0f;
  {   // DELAY THROW: the tracks' mix into the delay, faded
    const float w0 = M.w[P_THROW], w1 = m->punch >> P_THROW & 1 ? 1.0f : 0.0f;
    if (w0 != 0.0f || w1 != 0.0f)
      for (unsigned i = 0; i < n; ++i) dsum[i] += (w0 + (w1 - w0) * (i + 1) / n) * 0.5f * (l[i] + r[i]);
    M.w[P_THROW] = w1;
  }
  M.frozen = m->punch >> P_FREEZE & 1;
  if (M.frozen) {   // REVERB FREEZE: the mix in for a 1/16 after the press, then nothing in
    const float open = 0.25f * m->fpb - M.since[P_FREEZE];
    for (unsigned i = 0; i < n; ++i) fx[i].l = i < open ? fx[i].l + 0.5f * (l[i] + r[i]) : 0.0f;
  }

  for (unsigned i = 0; i < n; ++i) {   // this block's sends out, the last block's returns in
    const float send = fx[i].l;
    fx[i] = i < M.rn ? rio[i] : clouds::FloatFrame{0.0f, 0.0f};
    rio[i].l = rio[i].r = send;   // the reverb's input is mono
  }
  M.rn = n;
  M.rsize = m->rsize;
  M.rtone = m->rtone;
  delay(*m, n);
  for (unsigned i = 0; i < n; ++i) {
    l[i] += fx[i].l * dg[i];
    r[i] += fx[i].r * dg[i];
  }
  punches(*m, l, r, n);
  for (int k = 0; k < 16; ++k) M.since[k] += n;
  M.held = m->punch;

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

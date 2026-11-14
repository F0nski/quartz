// The six Plaits voices behind a C interface. Each voice renders Plaits blocks of kBlockSize
// frames into its own small FIFO, so the platform's audio block need not be a multiple of
// kBlockSize.

#include "pq.h"

#include <math.h>
#include <new>

#include "plaits/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "project.h"
#include "qvoice.h"

namespace {

const size_t kBufferSize = 16384;   // the engines' buffer, as on the module (plaits.cc)
const float kSilence = 1e-4f;
float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
const unsigned kSilentFrames = static_cast<unsigned>(0.05f * plaits::kSampleRate);

struct Slot {
  plaits::QVoice voice;
  plaits::Patch patch;
  plaits::Modulations mod;
  plaits::QVoice::Frame block[plaits::kBlockSize];
  float out[plaits::kBlockSize];   // block, filtered, in [-1, 1]
  stmlib::Svf svf;
  size_t pos;            // next frame to read in out (kBlockSize: empty)
  bool gate;
  bool retrigger;        // a note-on came while the voice last saw the trigger high
  float target;          // note the glide heads to
  float note;            // the glided note, without the LFO's pitch offset
  float harm, timb, morph;   // the patch's values, before the LFO's offsets
  float chr;             // out to aux: a KIT pad's CHAR (pq_set_char), 0 for the synths
  float vel;
  float env;             // the ADSR at the last block
  bool own_env;          // the engine has its own envelope: the gate sets the level, not the ADSR
  float glide;           // one-pole coefficient per block (1: immediate)
  unsigned quiet;        // frames since the output last exceeded kSilence, up to kSilentFrames
  char buffer[kBufferSize];
};

// Raw storage in the platform's pool, built with placement new in pq_init: the platform runs
// no C++ static constructors, and the engines' vtables need them.
alignas(Slot) char storage[PQ_VOICES][sizeof(Slot)] __attribute__((section(".pool")));
Slot* slots[PQ_VOICES];

// a frame in [-1, 1): the main output, or toward the aux by chr
inline float mix(const plaits::QVoice::Frame& f, float chr) {
  const float out = static_cast<float>(f.out);
  return (out + chr * (static_cast<float>(f.aux) - out)) * (1.0f / 32768.0f);
}

}  // namespace

#ifndef TEST                      // (the host tests link the DSP without cxx_runtime.cc)
extern "C" const unsigned char pq_text_pad[];
#endif

extern "C" void pq_init(void) {
#ifndef TEST
  (void)*(volatile const unsigned char*)pq_text_pad;   // (keeps the placement pad: cxx_runtime.cc)
#endif
  for (int i = 0; i < PQ_VOICES; ++i) {
    slots[i] = new (storage[i]) Slot();
    Slot& s = *slots[i];
    s.voice.Init(s.buffer, sizeof(s.buffer));
    s.patch.note = 0.0f;   // the note comes in through mod.note
    s.patch.harmonics = 0.5f;
    s.patch.timbre = 0.5f;
    s.patch.morph = 0.5f;
    s.harm = s.timb = s.morph = 0.5f;
    s.chr = 0.0f;
    s.patch.frequency_modulation_amount = 0.0f;
    s.patch.timbre_modulation_amount = 0.0f;
    s.patch.morph_modulation_amount = 0.0f;
    s.patch.engine = 8;
    s.patch.decay = 0.5f;
    s.patch.lpg_colour = 0.5f;
    s.mod.engine = 0.0f;
    s.mod.note = 60.0f;
    s.mod.frequency = 0.0f;
    s.mod.harmonics = 0.0f;
    s.mod.timbre = 0.0f;
    s.mod.morph = 0.0f;
    s.mod.trigger = 0.0f;
    s.mod.level = 0.0f;
    s.mod.frequency_patched = false;
    s.mod.timbre_patched = false;
    s.mod.morph_patched = false;
    s.mod.trigger_patched = true;   // the percussive models follow the trigger
    s.mod.level_patched = true;     // the LPG follows the gate, at the velocity's level
    s.pos = plaits::kBlockSize;
    s.gate = false;
    s.retrigger = false;
    s.target = 60.0f;
    s.note = 60.0f;
    s.vel = 0.0f;
    s.env = 0.0f;
    s.own_env = false;
    s.svf.Init();
    s.glide = 1.0f;
    s.quiet = kSilentFrames;
  }
}

extern "C" void pq_set_engine(int v, int model) {
  slots[v]->patch.engine = model;
  slots[v]->own_env = model == 2 || model == 3 || model == 4 || model == 19 || model == 20 || (model >= 21 && model <= 23);
  slots[v]->voice.SetEngine(model);
  slots[v]->chr = 0.0f;
}

extern "C" void pq_set_char(int v, float chr) { slots[v]->chr = chr; }

extern "C" void pq_set_patch(int v, float harm, float timb, float morph, float decay, float glide) {
  Slot& s = *slots[v];
  s.harm = harm;
  s.timb = timb;
  s.morph = morph;
  s.patch.decay = decay;
  // glide 1: time constant of 0.5 s
  const float tau = glide * 0.5f * plaits::kSampleRate / plaits::kBlockSize;
  s.glide = tau > 0.0f ? 1.0f - expf(-1.0f / tau) : 1.0f;
}

extern "C" void pq_note_on(int v, float note, float vel) {
  Slot& s = *slots[v];
  s.target = note;
  s.retrigger = s.mod.trigger > 0.5f;   // the trigger last sent, not the gate: off+on between renders
  s.gate = true;
  s.vel = vel;
  s.quiet = 0;
}

extern "C" void pq_note_off(int v) {
  slots[v]->gate = false;   // the level falls with the ADSR's release
}

extern "C" void pq_glide_to(int v, float note) { slots[v]->target = note; }

extern "C" void pq_render(int v, float* out, unsigned n) {
  Slot& s = *slots[v];
  for (unsigned i = 0; i < n; ++i) {
    if (s.pos >= plaits::kBlockSize) {
      pq_mod m;
      mod_voice_block(v, &m);
      s.env = m.env;
      s.note += s.glide * (s.target - s.note);
      s.mod.note = s.note + m.pitch;
      s.mod.level = s.own_env ? (s.gate ? s.vel : 0.0f) : s.vel * m.env;
      s.patch.harmonics = clamp01(s.harm + m.harm);
      s.patch.timbre = clamp01(s.timb + m.timb);
      s.patch.morph = clamp01(s.morph + m.morph);
      // A retrigger holds the trigger low for one block so that the voice sees a rising edge.
      s.mod.trigger = s.gate && !s.retrigger ? 1.0f : 0.0f;
      s.retrigger = false;
      s.voice.Render(s.patch, s.mod, s.block, plaits::kBlockSize);
      // LP, CUTOFF 127, RES 0 (the default) passes all the sound: not computed, a big share of the voices' time
      if (m.type == FT_LP && m.cutoff >= 0.45f && m.res <= 0.5f) {
        for (size_t k = 0; k < plaits::kBlockSize; ++k) {
          const float y = mix(s.block[k], s.chr) * m.gain;
          s.out[k] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
        }
      } else {
        s.svf.set_f_q<stmlib::FREQUENCY_ACCURATE>(m.cutoff, m.res);
        for (size_t k = 0; k < plaits::kBlockSize; ++k) {
          const float x = mix(s.block[k], s.chr);
          const float y = (m.type == FT_HP ? s.svf.Process<stmlib::FILTER_MODE_HIGH_PASS>(x)
                         : m.type == FT_BP ? s.svf.Process<stmlib::FILTER_MODE_BAND_PASS>(x)
                         : s.svf.Process<stmlib::FILTER_MODE_LOW_PASS>(x)) * m.gain;
          s.out[k] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;   // the resonance can overshoot
        }
      }
      s.pos = 0;
    }
    const float x = s.out[s.pos++];
    out[i] += x;
    if (x > kSilence || x < -kSilence) {   // fabsf is a call on the device
      s.quiet = 0;
    } else if (s.quiet < kSilentFrames) {
      ++s.quiet;
    }
  }
}

extern "C" int pq_silent(int v) {
  return !slots[v]->gate && slots[v]->env == 0.0f && slots[v]->quiet >= kSilentFrames;
}

extern "C" unsigned pq_voice_bytes(void) { return sizeof(Slot); }

// The six Plaits voices behind a C interface. Each voice renders Plaits blocks of kBlockSize
// frames into its own small FIFO, so the platform's audio block need not be a multiple of
// kBlockSize.

#include "pq.h"

#include <math.h>
#include <new>

#include "plaits/dsp/dsp.h"
#include "qvoice.h"

namespace {

const size_t kBufferSize = 16384;   // the engines' buffer, as on the module (plaits.cc)
const float kSilence = 1e-4f;
const unsigned kSilentFrames = static_cast<unsigned>(0.05f * plaits::kSampleRate);

struct Slot {
  plaits::QVoice voice;
  plaits::Patch patch;
  plaits::Modulations mod;
  plaits::QVoice::Frame block[plaits::kBlockSize];
  size_t pos;            // next frame to read in block (kBlockSize: empty)
  bool gate;
  bool retrigger;        // a note-on came while the gate was high
  float target;          // note the glide heads to
  float glide;           // one-pole coefficient per block (1: immediate)
  unsigned quiet;        // frames since the output last exceeded kSilence, up to kSilentFrames
  char buffer[kBufferSize];
};

// Raw storage in the platform's pool, built with placement new in pq_init: the platform runs
// no C++ static constructors, and the engines' vtables need them.
alignas(Slot) char storage[PQ_VOICES][sizeof(Slot)] __attribute__((section(".pool")));
Slot* slots[PQ_VOICES];

}  // namespace

extern "C" void pq_init(void) {
  for (int i = 0; i < PQ_VOICES; ++i) {
    slots[i] = new (storage[i]) Slot();
    Slot& s = *slots[i];
    s.voice.Init(s.buffer, sizeof(s.buffer));
    s.patch.note = 0.0f;   // the note comes in through mod.note
    s.patch.harmonics = 0.5f;
    s.patch.timbre = 0.5f;
    s.patch.morph = 0.5f;
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
    s.glide = 1.0f;
    s.quiet = kSilentFrames;
  }
}

extern "C" void pq_set_engine(int v, int model) {
  slots[v]->patch.engine = model;
  slots[v]->voice.SetEngine(model);
}

extern "C" void pq_set_patch(int v, float harm, float timb, float morph, float decay, float glide) {
  Slot& s = *slots[v];
  s.patch.harmonics = harm;
  s.patch.timbre = timb;
  s.patch.morph = morph;
  s.patch.decay = decay;
  // glide 1: time constant of 0.5 s
  const float tau = glide * 0.5f * plaits::kSampleRate / plaits::kBlockSize;
  s.glide = tau > 0.0f ? 1.0f - expf(-1.0f / tau) : 1.0f;
}

extern "C" void pq_note_on(int v, float note, float vel) {
  Slot& s = *slots[v];
  s.target = note;
  s.retrigger = s.gate;
  s.gate = true;
  s.mod.level = vel;
  s.quiet = 0;
}

extern "C" void pq_note_off(int v) {
  slots[v]->gate = false;
  slots[v]->mod.level = 0.0f;
}

extern "C" void pq_render(int v, float* out, unsigned n) {
  Slot& s = *slots[v];
  for (unsigned i = 0; i < n; ++i) {
    if (s.pos >= plaits::kBlockSize) {
      s.mod.note += s.glide * (s.target - s.mod.note);
      // A retrigger holds the trigger low for one block so that the voice sees a rising edge.
      s.mod.trigger = s.gate && !s.retrigger ? 1.0f : 0.0f;
      s.retrigger = false;
      s.voice.Render(s.patch, s.mod, s.block, plaits::kBlockSize);
      s.pos = 0;
    }
    const float x = static_cast<float>(s.block[s.pos++].out) * (1.0f / 32768.0f);
    out[i] += x;
    if (fabsf(x) > kSilence) {
      s.quiet = 0;
    } else if (s.quiet < kSilentFrames) {
      ++s.quiet;
    }
  }
}

extern "C" int pq_silent(int v) {
  return !slots[v]->gate && slots[v]->quiet >= kSilentFrames;
}

extern "C" unsigned pq_voice_bytes(void) { return sizeof(Slot); }

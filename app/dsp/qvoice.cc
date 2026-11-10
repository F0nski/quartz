// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Single-engine synthesis voice, derived from plaits/dsp/voice.cc (eurorack
// f4cfa769): the engine is built on demand instead of picked by the engine
// quantizer, and user data is never read (the FM-1 has none).

#include "qvoice.h"

#include <new>

#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"

#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const int kSpeechEngine = 15;
const int kChiptuneEngine = 7;

void QVoice::Init(void* buffer, size_t size) {
  buffer_ = buffer;
  buffer_size_ = size;
  engine_ = NULL;

  out_post_processor_.Init();
  aux_post_processor_.Init();

  decay_envelope_.Init();
  lpg_envelope_.Init();
  
  trigger_state_ = false;
  previous_note_ = 0.0f;
  
  trigger_delay_.Init(trigger_delay_line_);

  SetEngine(8);
}

template<typename T>
void QVoice::Construct(bool already_enveloped, float out_gain, float aux_gain) {
  static_assert(sizeof(T) <= sizeof(engine_storage_), "engine too large");
  static_assert(alignof(T) <= 8, "engine overaligned");
  engine_ = new (engine_storage_) T();
  PostProcessingSettings* s = &engine_->post_processing_settings;
  s->already_enveloped = already_enveloped;
  s->out_gain = out_gain;
  s->aux_gain = aux_gain;
}

void QVoice::SetEngine(int model) {
  if (engine_) {
    // The engines own nothing beyond the buffer: their destructors are empty.
    engine_->~Engine();
  }
  // Post-processing settings as registered in Voice::Init.
  switch (model) {
    case 0: Construct<VirtualAnalogVCFEngine>(false, 1.0f, 1.0f); break;
    case 1: Construct<PhaseDistortionEngine>(false, 0.7f, 0.7f); break;
    case 2:
    case 3:
    case 4: Construct<SixOpEngine>(true, 1.0f, 1.0f); break;
    case 5: Construct<WaveTerrainEngine>(false, 0.7f, 0.7f); break;
    case 6: Construct<StringMachineEngine>(false, 0.8f, 0.8f); break;
    case 7: Construct<ChiptuneEngine>(false, 0.5f, 0.5f); break;
    case 8: Construct<VirtualAnalogEngine>(false, 0.8f, 0.8f); break;
    case 9: Construct<WaveshapingEngine>(false, 0.7f, 0.6f); break;
    case 10: Construct<FMEngine>(false, 0.6f, 0.6f); break;
    case 11: Construct<GrainEngine>(false, 0.7f, 0.6f); break;
    case 12: Construct<AdditiveEngine>(false, 0.8f, 0.8f); break;
    case 13: Construct<WavetableEngine>(false, 0.6f, 0.6f); break;
    case 14: Construct<ChordEngine>(false, 0.8f, 0.8f); break;
    case 15: Construct<SpeechEngine>(false, -0.7f, 0.8f); break;
    case 16: Construct<SwarmEngine>(false, -3.0f, 1.0f); break;
    case 17: Construct<NoiseEngine>(false, -1.0f, -1.0f); break;
    case 18: Construct<ParticleEngine>(false, -2.0f, 1.0f); break;
    case 19: Construct<StringEngine>(true, -1.0f, 0.8f); break;
    case 20: Construct<ModalEngine>(true, -1.0f, 0.8f); break;
    case 21: Construct<BassDrumEngine>(true, 0.8f, 0.8f); break;
    case 22: Construct<SnareDrumEngine>(true, 0.8f, 0.8f); break;
    default: Construct<HiHatEngine>(true, 0.8f, 0.8f); break;
  }
  engine_index_ = model;

  BufferAllocator allocator(buffer_, buffer_size_);
  engine_->Init(&allocator);
  engine_->LoadUserData(
      model >= 2 && model <= 4 ? fm_patches_table[model - 2] : NULL);
  engine_->Reset();
  out_post_processor_.Reset();
}

void QVoice::Render(
    const Patch& patch,
    const Modulations& modulations,
    Frame* frames,
    size_t size) {
  // Trigger, LPG, internal envelope.
      
  // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
  // CV out lags behind the GATE out.
  trigger_delay_.Write(modulations.trigger);
  float trigger_value = trigger_delay_.Read(kTriggerDelay);
  
  bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_value > 0.3f) {
      trigger_state_ = true;
      if (!modulations.level_patched) {
        lpg_envelope_.Trigger();
      }
      decay_envelope_.Trigger();
    }
  } else {
    if (trigger_value < 0.1f) {
      trigger_state_ = false;
    }
  }

  Engine* e = engine_;
  EngineParameters p;

  bool rising_edge = trigger_state_ && !previous_trigger_state;
  float note = (modulations.note + previous_note_) * 0.5f;
  previous_note_ = modulations.note;
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | \
      (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }
  
  const float short_decay = (200.0f * kBlockSize) / kSampleRate *
      SemitonesToRatio(-96.0f * patch.decay);

  decay_envelope_.Process(short_decay * 2.0f);

  float compressed_level = 1.3f * modulations.level / (0.3f + fabsf(modulations.level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = modulations.level_patched ? compressed_level : 0.8f;

  bool use_internal_envelope = modulations.trigger_patched;

  // Actual synthesis parameters.
  
  p.harmonics = patch.harmonics + modulations.harmonics;
  CONSTRAIN(p.harmonics, 0.0f, 1.0f);

  float internal_envelope_amplitude = 1.0f;
  float internal_envelope_amplitude_timbre = 1.0f;
  if (engine_index_ == kSpeechEngine) {
    SpeechEngine* speech_engine = static_cast<SpeechEngine*>(e);
    internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    speech_engine->set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
    speech_engine->set_speed( 
        !modulations.trigger_patched || modulations.morph_patched ?
            0.0f : patch.morph_modulation_amount);
  } else if (engine_index_ == kChiptuneEngine) {
    ChiptuneEngine* chiptune_engine = static_cast<ChiptuneEngine*>(e);
    if (modulations.trigger_patched && !modulations.timbre_patched) {
      // Disable internal envelope on TIMBRE, and enable the envelope generator
      // built into the chiptune engine.
      internal_envelope_amplitude_timbre = 0.0f;
      chiptune_engine->set_envelope_shape(patch.timbre_modulation_amount);
    } else {
      chiptune_engine->set_envelope_shape(ChiptuneEngine::NO_ENVELOPE);
    }
  }
  
  p.note = ApplyModulations(
      patch.note + note,
      patch.frequency_modulation_amount,
      modulations.frequency_patched,
      modulations.frequency,
      use_internal_envelope,
      internal_envelope_amplitude * \
          decay_envelope_.value() * decay_envelope_.value() * 48.0f,
      1.0f,
      -119.0f,
      120.0f);

  p.timbre = ApplyModulations(
      patch.timbre,
      patch.timbre_modulation_amount,
      modulations.timbre_patched,
      modulations.timbre,
      use_internal_envelope,
      internal_envelope_amplitude_timbre * decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  p.morph = ApplyModulations(
      patch.morph,
      patch.morph_modulation_amount,
      modulations.morph_patched,
      modulations.morph,
      use_internal_envelope,
      internal_envelope_amplitude * decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  bool already_enveloped = pp_s.already_enveloped;
  e->Render(p, out_buffer_, aux_buffer_, size, &already_enveloped);
  
  bool lpg_bypass = already_enveloped || \
      (!modulations.level_patched && !modulations.trigger_patched);
  
  // Compute LPG parameters.
  if (!lpg_bypass) {
    const float hf = patch.lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-72.0f * patch.decay + 12.0f * hf) - short_decay;
    
    if (modulations.level_patched) {
      lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
    } else {
      const float attack = NoteToFrequency(p.note) * float(kBlockSize) * 2.0f;
      lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
    }
  } else {
    lpg_envelope_.Init();
  }
  
  out_post_processor_.Process(
      pp_s.out_gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      out_buffer_,
      &frames->out,
      size,
      2);

  aux_post_processor_.Process(
      pp_s.aux_gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      aux_buffer_,
      &frames->aux,
      size,
      2);
}
  
}  // namespace plaits

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
// Single-engine synthesis voice, derived from plaits/dsp/voice.h (eurorack
// f4cfa769). Plaits' Voice holds all 24 engines; QVoice holds only the selected
// one, so that six voices fit in the FM-1's RAM.

#ifndef QUARTZ_DSP_QVOICE_H_
#define QUARTZ_DSP_QVOICE_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "plaits/dsp/physical_modelling/delay_line.h"

namespace plaits {

const int kMaxTriggerDelay = 8;
const int kTriggerDelay = 5;

class ChannelPostProcessor {
 public:
  ChannelPostProcessor() { }
  ~ChannelPostProcessor() { }
  
  void Init() {
    lpg_.Init();
    Reset();
  }
  
  void Reset() {
    limiter_.Init();
  }
  
  void Process(
      float gain,
      bool bypass_lpg,
      float low_pass_gate_gain,
      float low_pass_gate_frequency,
      float low_pass_gate_hf_bleed,
      float* in,
      short* out,
      size_t size,
      size_t stride) {
    if (gain < 0.0f) {
      limiter_.Process(-gain, in, size);
    }
    const float post_gain = (gain < 0.0f ? 1.0f : gain) * -32767.0f;
    if (!bypass_lpg) {
      lpg_.Process(
          post_gain * low_pass_gate_gain,
          low_pass_gate_frequency,
          low_pass_gate_hf_bleed,
          in,
          out,
          size,
          stride);
    } else {
      while (size--) {
        *out = stmlib::Clip16(1 + static_cast<int32_t>(*in++ * post_gain));
        out += stride;
      }
    }
  }
  
 private:
  stmlib::Limiter limiter_;
  LowPassGate lpg_;
  
  DISALLOW_COPY_AND_ASSIGN(ChannelPostProcessor);
};

struct Patch {
  float note;
  float harmonics;
  float timbre;
  float morph;
  float frequency_modulation_amount;
  float timbre_modulation_amount;
  float morph_modulation_amount;

  int engine;
  float decay;
  float lpg_colour;
};

struct Modulations {
  float engine;
  float note;
  float frequency;
  float harmonics;
  float timbre;
  float morph;
  float trigger;
  float level;

  bool frequency_patched;
  bool timbre_patched;
  bool morph_patched;
  bool trigger_patched;
  bool level_patched;
};

class QVoice {
 public:
  QVoice() { }
  ~QVoice() { }
  
  struct Frame {
    short out;
    short aux;
  };
  
  // buffer: the engine's RAM, size bytes (Plaits shares 16384 between all
  // engines). Selects model 8 (VA).
  void Init(void* buffer, size_t size);
  // model: 0..23 in Plaits' registration order. Destroys the current engine
  // and builds this one in its place. As in Plaits' Voice, the decay and LPG
  // envelopes and the trigger state carry over: a sounding voice keeps
  // sounding through the switch.
  void SetEngine(int model);
  void Render(
      const Patch& patch,
      const Modulations& modulations,
      Frame* frames,
      size_t size);
  inline int active_engine() const { return engine_index_; }
    
 private:
  template<typename T>
  void Construct(bool already_enveloped, float out_gain, float aux_gain);

  inline float ApplyModulations(
      float base_value,
      float modulation_amount,
      bool use_external_modulation,
      float external_modulation,
      bool use_internal_envelope,
      float envelope,
      float default_internal_modulation,
      float minimum_value,
      float maximum_value) {
    float value = base_value;
    modulation_amount *= std::max(fabsf(modulation_amount) - 0.05f, 0.05f);
    modulation_amount *= 1.05f;
    
    float modulation = use_external_modulation
        ? external_modulation
        : (use_internal_envelope ? envelope : default_internal_modulation);
    value += modulation_amount * modulation;
    CONSTRAIN(value, minimum_value, maximum_value);
    return value;
  }

  // Room for the largest engine; Construct() static_asserts every engine fits.
  alignas(8) char engine_storage_[sizeof(SixOpEngine)];
  Engine* engine_;
  int engine_index_;
  void* buffer_;
  size_t buffer_size_;
  
  float previous_note_;
  bool trigger_state_;
  
  DecayEnvelope decay_envelope_;
  LPGEnvelope lpg_envelope_;
  
  float trigger_delay_line_[kMaxTriggerDelay];
  DelayLine<float, kMaxTriggerDelay> trigger_delay_;
  
  ChannelPostProcessor out_post_processor_;
  ChannelPostProcessor aux_post_processor_;
  
  float out_buffer_[kMaxBlockSize];
  float aux_buffer_[kMaxBlockSize];
  
  DISALLOW_COPY_AND_ASSIGN(QVoice);
};

}  // namespace plaits

#endif  // QUARTZ_DSP_QVOICE_H_

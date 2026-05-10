// src/voice.h — single oscillator voice with phase accumulator.
#pragma once

namespace sawstack {

class Voice {
  public:
    void  Init(float sample_rate);
    void  SetFrequency(float hz);  // Updates per-sample phase increment.
    float NaiveSaw();              // Advances phase, returns phase*2-1 in [-1, 1).
    float Saw();   // PolyBLEP-corrected saw, anti-aliased.

  private:
    float sample_rate_ = 48000.0f;
    float phase_       = 0.0f;     // [0, 1)
    float phase_inc_   = 0.0f;     // freq / sample_rate
};

}  // namespace sawstack

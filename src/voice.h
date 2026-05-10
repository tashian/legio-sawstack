// src/voice.h — single oscillator voice with phase accumulator.
#pragma once

namespace sawstack {

class Voice {
  public:
    void  Init(float sample_rate);
    void  SetFrequency(float hz);  // Updates per-sample phase increment.
    float NaiveSaw();              // Advances phase, returns phase*2-1 in [-1, 1).

  private:
    float sample_rate_ = 48000.0f;
    double phase_      = 0.0;      // [0, 1) - double precision to reduce accumulation error
    double phase_inc_  = 0.0;      // freq / sample_rate
};

}  // namespace sawstack

// src/voice.h — single oscillator voice with phase accumulator.
#pragma once

namespace sawstack {

class Voice {
  public:
    void  Init(float sample_rate);
    void  SetFrequency(float hz);  // Updates per-sample phase increment.
    float NaiveSaw();              // Advances phase, returns phase*2-1 in [-1, 1).
    float Saw();   // PolyBLEP-corrected saw, anti-aliased.
    float Sine();                          // Pure sine at the current frequency.
    float Morph(float timbre);             // (1-timbre) * sine + timbre * polyBLEP saw.
    void  ResetPhase(float new_phase);     // Sets phase directly. new_phase ∈ [0, 1).
    float HardSync(float ratio);   // Master at SetFrequency, slave at master*ratio,
                                   // slave is hard-synced when master wraps.
                                   // Returns polyBLEP-corrected saw of slave phase.
    void  ResetSyncPhase();        // Zero the slave phase only.

  private:
    float sample_rate_ = 48000.0f;
    float phase_       = 0.0f;     // [0, 1)
    float phase_inc_   = 0.0f;     // freq / sample_rate
    float slave_phase_      = 0.0f;  // [0, 1) — advances at master_freq * ratio in RICH
    bool  slave_just_reset_ = false; // suppress polyBLEP on first sample after hard-sync
};

}  // namespace sawstack

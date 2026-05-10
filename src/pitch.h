// src/pitch.h
#pragma once

namespace sawstack {

// V/oct calibration. Measure once on real hardware, edit, rebuild, reflash.
// kVoctScale = 0 disables the v/oct path entirely (jack ignored).
constexpr float kVoctZero  = 0.0f;   // ADC reading at 0V (placeholder; Task 17 measures)
constexpr float kVoctScale = 0.0f;   // 1.0 / (adc_at_+1V - adc_at_0V) (placeholder)

// Convert raw v/oct ADC reading to volts using calibration constants.
// Returns 0.0 if scale == 0.0 (calibration not done).
float VoctVoltsFromAdc(float voct_adc, float zero, float scale);

// Compute the master pitch in Hz.
//   coarse_semitones: encoder coarse offset in semitones, [-24, +24]
//   fine_cents:       encoder fine offset in cents,      [-50, +50]
//   voct_volts:       v/oct CV in volts (already converted)
float ComputeMasterHz(int coarse_semitones, int fine_cents, float voct_volts);

}  // namespace sawstack

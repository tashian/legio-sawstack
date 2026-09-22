// src/pitch.h
#pragma once

namespace sawstack {

// V/oct calibration — per-module. The Patch SM's ADC offset/gain vary slightly
// between units, so the defaults below (measured on the author's module) will be
// a little off on yours. To use your own values without touching tracked files,
// copy calibration_local.h.example to calibration_local.h (gitignored) and edit
// it; see README "Calibrating v/oct". kVoctScale = 0 disables the v/oct path.
#if __has_include("calibration_local.h")
#include "calibration_local.h"
#else
constexpr float kVoctZero  = 0.3019f;   // ADC reading at 0 V
constexpr float kVoctScale = 7.6805f;   // 1.0 / (0.4321 - 0.3019); linear to ~1 LSB at -2 V
#endif

// Convert raw v/oct ADC reading to volts using calibration constants.
// Returns 0.0 if scale == 0.0 (calibration not done).
float VoctVoltsFromAdc(float voct_adc, float zero, float scale);

// Compute the master pitch in Hz.
//   coarse_semitones: encoder coarse offset in semitones, [-48, +48]
//   fine_cents:       encoder fine offset in cents,      [-50, +50]
//   voct_volts:       v/oct CV in volts (already converted)
float ComputeMasterHz(int coarse_semitones, int fine_cents, float voct_volts);

// Asymmetric detune multipliers — ~1% asymmetric to avoid octave/fifth coincidences.
// Multipliers for voices 0..4 of the absolute offset magnitude (voice 0 is always center).
// Tune by ear during first flash session; current values are first-pass guesses.
constexpr float kDetuneMul[5] = { 0.0f, 0.500f, -0.4985f, 1.013f, -1.011f };
//                                 v0    v1      v2        v3     v4

// Fill `out_freqs[5]` with the per-voice frequencies given a master pitch and
// a normalized detune in [0, 1]. Spread is squared (detune_norm² * 2400 cents)
// for fine control near unison.
void ComputeVoiceFreqs(float master_hz, float detune_norm, float out_freqs[5]);

}  // namespace sawstack

// src/pitch.cpp
#include "pitch.h"
#include <cmath>

namespace sawstack {

float VoctVoltsFromAdc(float voct_adc, float zero, float scale) {
    if (scale == 0.0f) return 0.0f;
    return (voct_adc - zero) * scale;
}

float ComputeMasterHz(int coarse_semitones, int fine_cents, float voct_volts) {
    constexpr float kC4Hz = 261.63f;
    float exponent = (coarse_semitones / 12.0f)
                   + (fine_cents       / 1200.0f)
                   + voct_volts;
    return kC4Hz * std::pow(2.0f, exponent);
}

void ComputeVoiceFreqs(float master_hz, float detune_norm, float out_freqs[5]) {
    if (detune_norm < 0.0f) detune_norm = 0.0f;
    if (detune_norm > 1.0f) detune_norm = 1.0f;
    float spread_cents = detune_norm * detune_norm * 2400.0f;
    for (int i = 0; i < 5; ++i) {
        float offset_cents = kDetuneMul[i] * spread_cents;
        out_freqs[i] = master_hz * std::pow(2.0f, offset_cents / 1200.0f);
    }
}

}  // namespace sawstack

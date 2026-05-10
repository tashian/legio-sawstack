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

}  // namespace sawstack

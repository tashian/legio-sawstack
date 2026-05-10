// src/voice.cpp
#include "voice.h"
#include <cmath>

namespace sawstack {

void Voice::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    phase_       = 0.0f;
    phase_inc_   = 0.0f;
}

void Voice::SetFrequency(float hz) {
    phase_inc_ = hz / sample_rate_;
}

float Voice::NaiveSaw() {
    double normalized_phase = std::fmod(phase_, 1.0);
    if (normalized_phase < 0.0) normalized_phase += 1.0;
    float out = (float)(normalized_phase * 2.0 - 1.0);
    phase_ += phase_inc_;
    return out;
}

}  // namespace sawstack

// src/voice.cpp
#include "voice.h"

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
    float out = phase_ * 2.0f - 1.0f;
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}

}  // namespace sawstack

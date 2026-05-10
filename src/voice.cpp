// src/voice.cpp
#include "voice.h"

namespace {
// PolyBLEP correction: returns a small adjustment to apply near phase
// wrap-around to suppress aliasing from the discontinuity.
inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
}

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

float Voice::Saw() {
    float out = phase_ * 2.0f - 1.0f;
    out -= poly_blep(phase_, phase_inc_);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}

}  // namespace sawstack

// src/voice.cpp
#include "voice.h"
#include <cmath>

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

float Voice::Sine() {
    float out = std::sin(phase_ * 2.0f * 3.14159265358979f);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}

float Voice::Morph(float timbre) {
    // Compute both shapes from the *same* phase before advancing.
    float s = std::sin(phase_ * 2.0f * 3.14159265358979f);
    float saw = phase_ * 2.0f - 1.0f - poly_blep(phase_, phase_inc_);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return (1.0f - timbre) * s + timbre * saw;
}

void Voice::ResetPhase(float new_phase) {
    phase_ = new_phase;
}

float Voice::HardSync(float ratio) {
    // Output is polyBLEP saw of the slave phase.
    float slave_inc = phase_inc_ * ratio;
    float out = slave_phase_ * 2.0f - 1.0f;
    out -= poly_blep(slave_phase_, slave_inc);

    // Advance master; on wrap, hard-sync the slave to phase 0.
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) {
        phase_       -= 1.0f;
        slave_phase_  = 0.0f;
    } else {
        slave_phase_ += slave_inc;
        if (slave_phase_ >= 1.0f) slave_phase_ -= 1.0f;
    }
    return out;
}

void Voice::ResetSyncPhase() {
    slave_phase_ = 0.0f;
}

}  // namespace sawstack

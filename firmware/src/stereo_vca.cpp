// src/stereo_vca.cpp
#include "stereo_vca.h"
#include <cmath>

namespace sawstack {
namespace {

// Equal-power pan: pan ∈ [-1, +1], returns (gainL, gainR), sum-of-squares = 1.
inline void pan_gains(float pan, float* gl, float* gr) {
    float angle = (pan + 1.0f) * 3.14159265358979f * 0.25f;  // 0..π/2
    *gl = std::cos(angle);
    *gr = std::sin(angle);
}

// Voice indices in pitch order (lowest → highest):
// voice 4 (offset −1.0×), voice 2 (−0.5×), voice 0 (0), voice 1 (+0.5×), voice 3 (+1.0×).
constexpr int kPitchOrder[5] = { 4, 2, 0, 1, 3 };

// Pan positions per width, in pitch order.
constexpr float kPanMono[5]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kPanStereo[5] = { -0.5f, -0.25f, 0.0f, +0.25f, +0.5f };
constexpr float kPanWide[5]   = { -1.0f, -0.5f,  0.0f, +0.5f,  +1.0f };

}  // namespace

void MixVoices(const float voices[5], Width w, float* out_l, float* out_r) {
    const float* pans = (w == Width::MONO)   ? kPanMono
                      : (w == Width::STEREO) ? kPanStereo
                                             : kPanWide;
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 5; ++i) {
        int   v_idx = kPitchOrder[i];
        float gl, gr;
        pan_gains(pans[i], &gl, &gr);
        l += voices[v_idx] * gl;
        r += voices[v_idx] * gr;
    }
    *out_l = l;
    *out_r = r;
}

}  // namespace sawstack

// src/stereo_vca.h
#pragma once
#include "params.h"

namespace sawstack {

// 5-voice MONO/STEREO/WIDE mixer with equal-power pan law.
// Voice indices in pitch order (low → high pitch): 4, 2, 0, 1, 3.
// MONO  : all voices summed equally to L and R.
// STEREO: pan positions in pitch order: -0.5, -0.25, 0, +0.25, +0.5
// WIDE  : pan positions in pitch order: -1.0, -0.5,  0, +0.5,  +1.0
void MixVoices(const float voices[5], Width w, float* out_l, float* out_r);

}  // namespace sawstack

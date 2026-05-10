// src/soft_clip.h — piecewise polynomial soft-clip, ported from legio_tape.
#pragma once

namespace sawstack {

// Output is bounded to [-2/3, 2/3]. Smooth derivative across the whole range.
inline float soft_clip(float x) {
    if (x >  1.0f) return  2.0f / 3.0f;
    if (x < -1.0f) return -2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

}  // namespace sawstack

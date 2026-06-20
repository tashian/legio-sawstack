// src/params.h
#pragma once
#include <cstdint>

namespace sawstack {

enum class Mode : uint8_t  { STACK = 0, RICH = 1, SUB = 2 };
enum class Width : uint8_t { MONO  = 0, STEREO = 1, WIDE = 2 };

struct Params {
    // ADCs in [0, 1]
    float top_adc;       // detune knob + top CV (summed)
    float bottom_adc;    // morph knob + bottom CV (summed)
    float voct_adc;      // raw v/oct ADC (pre-zero/scale)

    Mode  mode;          // from left switch (panel-corrected)
    Width width;         // from right switch (panel-corrected)

    // Encoder events accumulated since last apply_params.
    int   encoder_fine_delta;    // detents (no press)
    int   encoder_coarse_delta;  // detents (press + turn)

    bool  gate_edge;     // true on rising edge this block

    // Plugin-only: absolute master pitch in Hz. <= 0 ignores this and uses the
    // encoder/voct path above. Firmware never sets it (defaults to 0).
    float external_hz = 0.0f;
};

}  // namespace sawstack

// src/leds.h
#pragma once
#include "params.h"
#include <cstdint>

namespace sawstack {

struct Rgb { float r, g, b; };

struct LedInputs {
    Mode  mode;
    Width width;
    uint32_t now_ms;                     // current millisecond counter
    bool gate_high;                       // live gate level (true while gate input is HIGH)
    uint32_t last_octave_crossing_ms;    // ms timestamp of most recent octave boundary cross (0 if none)
    uint32_t boot_ms;                    // ms since boot
};

void ComputeLeds(const LedInputs& in, Rgb* out_left, Rgb* out_right);

}  // namespace sawstack

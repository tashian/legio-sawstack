// src/leds.h
#pragma once
#include "params.h"
#include <cstdint>

namespace sawstack {

struct Rgb { float r, g, b; };

struct LedInputs {
    Mode  mode;
    Width width;
    uint32_t now_ms;                  // current millisecond counter
    uint32_t last_gate_edge_ms;       // ms timestamp of most recent rising edge (0 if none)
    uint32_t boot_ms;                 // ms since boot
};

void ComputeLeds(const LedInputs& in, Rgb* out_left, Rgb* out_right);

}  // namespace sawstack

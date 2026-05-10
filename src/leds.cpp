// src/leds.cpp
#include "leds.h"

namespace sawstack {

namespace {
constexpr uint32_t kBootDurationMs   = 500;
constexpr uint32_t kBootBlinkMs      = 125;   // 4 Hz total period 250 ms; on for 125 ms
constexpr uint32_t kGatePulseMs      = 50;
constexpr uint32_t kOctavePulseMs    = 80;     // brief LEDs-on-bright flash on octave cross
constexpr float    kGatePulseGain    = 1.4f;  // brightness multiplier during pulse

constexpr Rgb kBlue       = { 0.0f, 0.0f, 0.8f };
constexpr Rgb kYellow     = { 0.7f, 0.6f, 0.0f };
constexpr Rgb kDimWhite   = { 0.25f, 0.25f, 0.25f };
constexpr Rgb kBarelyOn   = { 0.05f, 0.05f, 0.05f };
constexpr Rgb kStereoBlue = { 0.0f, 0.0f, 0.40f };
constexpr Rgb kWideBlue   = { 0.0f, 0.0f, 0.90f };

inline Rgb scale(Rgb c, float k) { return { c.r * k, c.g * k, c.b * k }; }

inline Rgb mode_color(Mode m) {
    switch (m) {
        case Mode::STACK: return kBlue;
        case Mode::RICH:  return kYellow;
        case Mode::SUB:   return kDimWhite;
    }
    return kDimWhite;
}

inline Rgb width_color(Width w) {
    switch (w) {
        case Width::MONO:   return kBarelyOn;
        case Width::STEREO: return kStereoBlue;
        case Width::WIDE:   return kWideBlue;
    }
    return kBarelyOn;
}
}  // namespace

void ComputeLeds(const LedInputs& in, Rgb* out_left, Rgb* out_right) {
    // Boot ID pattern: alternate blue/yellow at 4 Hz for 500 ms.
    if (in.boot_ms < kBootDurationMs) {
        bool phase = (in.boot_ms / kBootBlinkMs) & 1;
        *out_left  = phase ? kBlue   : kYellow;
        *out_right = phase ? kYellow : kBlue;
        return;
    }

    // Normal mode display.
    Rgb l = mode_color(in.mode);
    Rgb r = width_color(in.width);

    // Gate-edge pulse on left LED.
    uint32_t since_gate = in.now_ms - in.last_gate_edge_ms;
    if (in.last_gate_edge_ms != 0 && since_gate < kGatePulseMs) {
        l = scale(l, kGatePulseGain);
        if (l.r > 1.0f) l.r = 1.0f;
        if (l.g > 1.0f) l.g = 1.0f;
        if (l.b > 1.0f) l.b = 1.0f;
    }

    // Octave-crossing flash: both LEDs briefly bright white. Overrides mode/width.
    uint32_t since_oct = in.now_ms - in.last_octave_crossing_ms;
    if (in.last_octave_crossing_ms != 0 && since_oct < kOctavePulseMs) {
        constexpr Rgb kFlash = { 0.9f, 0.9f, 0.9f };
        l = kFlash;
        r = kFlash;
    }

    *out_left  = l;
    *out_right = r;
}

}  // namespace sawstack

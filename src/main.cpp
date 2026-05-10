// src/main.cpp — Legio HAL + slow loop. Only file that includes daisy_legio.h.
#include "daisy_legio.h"
#include "supersaw_engine.h"
#include "params.h"
#include "dsp_common.h"
#include <cstdio>

using namespace daisy;
using sawstack::SupersawEngine;
using sawstack::Params;
using sawstack::Mode;
using sawstack::Width;

DaisyLegio        hw;
SupersawEngine    engine;

// Volatile UI snapshot updated from audio callback, drained by slow loop.
struct UiSnapshot {
    Mode  mode;
    Width width;
    float top_adc;
    float bottom_adc;
    float voct_adc;
    int   gate_edge_count;
};
volatile UiSnapshot g_ui = {};

// Convert libDaisy Switch3.Read() (0=CENTER, 1=POS_UP, 2=POS_DOWN) to a
// panel-corrected code (1 = panel UP, 2 = panel DOWN, 0 = CENTER).
// On Legio's panel POS_UP corresponds to **panel DOWN** and vice versa
// (verified by feel during legio_tape).
static int panel_relative_switch(uint8_t lib_pos) {
    switch (lib_pos) {
        case 0: return 0;  // CENTER
        case 1: return 2;  // lib POS_UP   = panel DOWN
        case 2: return 1;  // lib POS_DOWN = panel UP
    }
    return 0;
}

static Mode mode_from_left_switch(uint8_t lib_pos) {
    int p = panel_relative_switch(lib_pos);
    // 1 = panel UP → STACK; 0 = CENTER → RICH; 2 = panel DOWN → SUB
    if (p == 1) return Mode::STACK;
    if (p == 2) return Mode::SUB;
    return Mode::RICH;
}

static Width width_from_right_switch(uint8_t lib_pos) {
    int p = panel_relative_switch(lib_pos);
    // 1 = panel UP → MONO; 0 = CENTER → STEREO; 2 = panel DOWN → WIDE
    if (p == 1) return Width::MONO;
    if (p == 2) return Width::WIDE;
    return Width::STEREO;
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t size) {
    hw.ProcessAllControls();

    Params p {};
    p.top_adc    = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_TOP);
    p.bottom_adc = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_BOTTOM);
    p.voct_adc   = hw.GetKnobValue(DaisyLegio::CONTROL_PITCH);
    p.mode       = mode_from_left_switch(hw.sw[DaisyLegio::SW_LEFT].Read());
    p.width      = width_from_right_switch(hw.sw[DaisyLegio::SW_RIGHT].Read());

    // Encoder: accumulate increments since last block. If pressed, route to coarse;
    // otherwise to fine.
    int  incr    = (int)hw.encoder.Increment();
    bool pressed = hw.encoder.Pressed();
    if (pressed) {
        p.encoder_coarse_delta = incr;
        p.encoder_fine_delta   = 0;
    } else {
        p.encoder_coarse_delta = 0;
        p.encoder_fine_delta   = incr;
    }

    // Gate edge detection — block-rate via libDaisy's GateIn::Trig().
    p.gate_edge = hw.gate.Trig();

    engine.ApplyParams(p);

    // Stereo-interleaved out. `size` is the total number of samples (L+R).
    int n_frames = size / 2;
    static float l_buf[sawstack::kAudioBlockSize];
    static float r_buf[sawstack::kAudioBlockSize];
    engine.ProcessBlock(l_buf, r_buf, n_frames);
    for (int i = 0; i < n_frames; ++i) {
        out[2*i + 0] = l_buf[i];
        out[2*i + 1] = r_buf[i];
    }

    // Update UI snapshot.
    UiSnapshot& g = const_cast<UiSnapshot&>(g_ui);
    g.mode       = p.mode;
    g.width      = p.width;
    g.top_adc    = p.top_adc;
    g.bottom_adc = p.bottom_adc;
    g.voct_adc   = p.voct_adc;
    if (p.gate_edge) ++g.gate_edge_count;
}

static const char* mode_label(Mode m) {
    switch (m) { case Mode::STACK: return "STACK";
                 case Mode::RICH:  return "RICH";
                 case Mode::SUB:   return "SUB"; }
    return "?";
}
static const char* width_label(Width w) {
    switch (w) { case Width::MONO:   return "MONO";
                 case Width::STEREO: return "STEREO";
                 case Width::WIDE:   return "WIDE"; }
    return "?";
}

int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(sawstack::kAudioBlockSize);
    engine.Init(sawstack::kSampleRate);
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    hw.seed.StartLog(false);

    uint32_t last_print = System::GetNow();
    while (true) {
        if (System::GetNow() - last_print >= 200) {  // 5 Hz
            UiSnapshot snap = const_cast<const UiSnapshot&>(g_ui);  // copy out of volatile
            hw.seed.PrintLine("mode=%s width=%s top=%0.3f bot=%0.3f voct_raw=%0.4f sync=%d",
                              mode_label(snap.mode), width_label(snap.width),
                              snap.top_adc, snap.bottom_adc, snap.voct_adc,
                              snap.gate_edge_count);
            last_print = System::GetNow();
        }
        System::Delay(5);
    }
}

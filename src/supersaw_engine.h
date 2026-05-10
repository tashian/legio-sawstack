// src/supersaw_engine.h
#pragma once
#include "params.h"
#include "voice.h"

namespace sawstack {

class SupersawEngine {
  public:
    void Init(float sample_rate);

    // Called once per audio block, before ProcessBlock.
    void ApplyParams(const Params& p);

    // Generate `n_frames` of stereo audio into `out_l`, `out_r` (both size >= n_frames).
    void ProcessBlock(float* out_l, float* out_r, int n_frames);

  private:
    float sample_rate_     = 48000.0f;
    Mode  current_mode_    = Mode::STACK;
    Width current_width_   = Width::STEREO;

    float detune_norm_     = 0.0f;
    float morph_norm_      = 0.0f;
    int   coarse_semitones_= 0;
    int   fine_cents_      = 0;
    float voct_volts_      = 0.0f;

    Voice voices_[5];   // main supersaw voices
    Voice sub_voice_;   // SUB-mode sub-octave (used only in SUB)
};

}  // namespace sawstack

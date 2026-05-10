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

    Voice voices_[5];   // main supersaw voices
    Voice sub_voice_;   // SUB-mode sub-octave (used only in SUB)

    Mode  prev_mode_           = Mode::STACK;
    int   crossfade_remaining_ = 0;        // samples remaining in the 5 ms cosine fade
    int   crossfade_total_     = 0;        // total samples for current crossfade
    bool  gate_pending_        = false;
    // One-pole smoothers for ADCs.
    float detune_smooth_       = 0.0f;
    float morph_smooth_        = 0.0f;
    float voct_smooth_         = 0.0f;

    // Last output sample, used as the start of a mode-change crossfade.
    float last_l_              = 0.0f;
    float last_r_              = 0.0f;

    // Helper used during crossfade — generates one sample of the chosen mode.
    float generate_voice(int voice_idx, Mode m, float morph_for_block);
};

}  // namespace sawstack

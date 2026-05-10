// src/supersaw_engine.cpp
#include "supersaw_engine.h"
#include "pitch.h"
#include "stereo_vca.h"
#include "soft_clip.h"
#include <algorithm>
#include <cmath>

namespace sawstack {

void SupersawEngine::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    for (int i = 0; i < 5; ++i) voices_[i].Init(sample_rate);
    sub_voice_.Init(sample_rate);
    current_mode_  = Mode::STACK;
    current_width_ = Width::STEREO;
    detune_norm_   = 0.0f;
    morph_norm_    = 0.0f;
}

void SupersawEngine::ApplyParams(const Params& p) {
    // Mode change kicks off a 5 ms cosine crossfade.
    if (p.mode != current_mode_) {
        prev_mode_           = current_mode_;
        current_mode_        = p.mode;
        crossfade_total_     = static_cast<int>(sample_rate_ * 0.005f);  // 5 ms
        crossfade_remaining_ = crossfade_total_;
    }
    current_width_ = p.width;

    // Smoothing — one-pole IIR with ~5 ms time constant at audio rate.
    // Applied per-block; that's good enough to mask ADC jitter.
    constexpr float kAlpha = 0.2f;  // tau ≈ 5 ms at 48-sample blocks @ 48 kHz
    detune_smooth_ += kAlpha * (std::clamp(p.top_adc,    0.0f, 1.0f) - detune_smooth_);
    morph_smooth_  += kAlpha * (std::clamp(p.bottom_adc, 0.0f, 1.0f) - morph_smooth_);

    coarse_semitones_ += p.encoder_coarse_delta;
    fine_cents_       += p.encoder_fine_delta;
    if (coarse_semitones_ < -24) coarse_semitones_ = -24;
    if (coarse_semitones_ >  24) coarse_semitones_ =  24;
    if (fine_cents_       < -50) fine_cents_       = -50;
    if (fine_cents_       >  50) fine_cents_       =  50;

    voct_smooth_ += kAlpha * (VoctVoltsFromAdc(p.voct_adc, kVoctZero, kVoctScale) - voct_smooth_);

    float master_hz = ComputeMasterHz(coarse_semitones_, fine_cents_, voct_smooth_);
    float freqs[5];
    ComputeVoiceFreqs(master_hz, detune_smooth_, freqs);
    for (int i = 0; i < 5; ++i) voices_[i].SetFrequency(freqs[i]);
    sub_voice_.SetFrequency(master_hz * 0.5f);

    if (p.gate_edge) gate_pending_ = true;

    // Cache for ProcessBlock.
    detune_norm_ = detune_smooth_;
    morph_norm_  = morph_smooth_;
}

float SupersawEngine::generate_voice(int v, Mode m, float morph_for_block) {
    switch (m) {
        case Mode::STACK: return voices_[v].Morph(morph_for_block);
        case Mode::RICH:  return voices_[v].HardSync(1.0f + morph_for_block * 4.0f);
        case Mode::SUB:   return voices_[v].Morph(1.0f);
    }
    return 0.0f;
}

void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    constexpr float kVoiceMixGain = 0.25f;

    if (gate_pending_) {
        constexpr float kPhases[5] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f };
        for (int i = 0; i < 5; ++i) {
            voices_[i].ResetPhase(kPhases[i]);
            voices_[i].ResetSyncPhase();
        }
        sub_voice_.ResetPhase(0.0f);
        gate_pending_ = false;
    }

    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        for (int v = 0; v < 5; ++v)
            voice_samples[v] = generate_voice(v, current_mode_, morph_norm_);

        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        l *= kVoiceMixGain;
        r *= kVoiceMixGain;
        if (current_mode_ == Mode::SUB) {
            float sub = sub_voice_.Saw() * morph_norm_;
            l += sub;
            r += sub;
        }

        // Click-suppression ramp on mode change: cosine from 0 → 1 over the window.
        // Blends from the last output of the previous mode toward the new mode output,
        // so there is no step discontinuity at the block boundary.
        if (crossfade_remaining_ > 0) {
            float t = 1.0f - static_cast<float>(crossfade_remaining_) / crossfade_total_;
            float gain = 0.5f - 0.5f * std::cos(t * 3.14159265358979f);
            l = last_l_ * (1.0f - gain) + l * gain;
            r = last_r_ * (1.0f - gain) + r * gain;
            --crossfade_remaining_;
        }

        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
        last_l_ = out_l[n];
        last_r_ = out_r[n];
    }
}

}  // namespace sawstack

// src/supersaw_engine.cpp
#include "supersaw_engine.h"
#include "pitch.h"
#include "stereo_vca.h"
#include "soft_clip.h"
#include <algorithm>

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
    current_mode_  = p.mode;
    current_width_ = p.width;
    detune_norm_   = std::clamp(p.top_adc,    0.0f, 1.0f);
    morph_norm_    = std::clamp(p.bottom_adc, 0.0f, 1.0f);

    coarse_semitones_ += p.encoder_coarse_delta;
    fine_cents_       += p.encoder_fine_delta;
    if (coarse_semitones_ < -24) coarse_semitones_ = -24;
    if (coarse_semitones_ >  24) coarse_semitones_ =  24;
    if (fine_cents_       < -50) fine_cents_       = -50;
    if (fine_cents_       >  50) fine_cents_       =  50;

    voct_volts_ = VoctVoltsFromAdc(p.voct_adc, kVoctZero, kVoctScale);

    float master_hz = ComputeMasterHz(coarse_semitones_, fine_cents_, voct_volts_);
    float freqs[5];
    ComputeVoiceFreqs(master_hz, detune_norm_, freqs);
    for (int i = 0; i < 5; ++i) voices_[i].SetFrequency(freqs[i]);
    sub_voice_.SetFrequency(master_hz * 0.5f);  // octave below master
}

void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    constexpr float kVoiceMixGain = 0.25f;  // headroom so 5-voice sum doesn't crush sub

    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        switch (current_mode_) {
            case Mode::STACK:
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(morph_norm_);
                break;
            case Mode::RICH: {
                float ratio = 1.0f + morph_norm_ * 4.0f;
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].HardSync(ratio);
                break;
            }
            case Mode::SUB:
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(1.0f);
                break;
        }
        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        l *= kVoiceMixGain;
        r *= kVoiceMixGain;
        if (current_mode_ == Mode::SUB) {
            float sub = sub_voice_.Saw() * morph_norm_;
            l += sub;
            r += sub;
        }
        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
    }
}

}  // namespace sawstack

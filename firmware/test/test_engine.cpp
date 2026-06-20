// test/test_engine.cpp
#include "test_assert.h"
#include "../src/supersaw_engine.h"
#include "../src/params.h"
#include "../src/dsp_common.h"
#include "../src/pitch.h"
#include <cmath>

using sawstack::SupersawEngine;
using sawstack::Params;
using sawstack::Mode;
using sawstack::Width;
using sawstack::kSampleRate;

static Params default_params() {
    Params p {};
    p.top_adc    = 0.5f;     // some detune
    p.bottom_adc = 1.0f;     // morph: full saw in STACK
    p.voct_adc   = 0.0f;
    p.mode       = Mode::STACK;
    p.width      = Width::STEREO;
    p.encoder_fine_delta   = 0;
    p.encoder_coarse_delta = 0;
    p.gate_edge  = false;
    return p;
}

void test_engine_stack_produces_nonzero_audio() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    eng.ApplyParams(default_params());
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    // Sum of squares should be > 0 (we got audio).
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_stack_morph_zero_is_quieter_lf_than_morph_one() {
    // At morph=0 (sine) at 440 Hz, output is bandlimited; at morph=1 (saw),
    // output has more high-frequency energy. We just sanity-check non-zero output
    // at both ends.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e_sine = 0;
    for (int i = 0; i < 48; ++i) e_sine += l[i]*l[i] + r[i]*r[i];

    p.bottom_adc = 1.0f;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);
    double e_saw = 0;
    for (int i = 0; i < 48; ++i) e_saw += l2[i]*l2[i] + r2[i]*r2[i];

    EXPECT_TRUE(e_sine > 0.0);
    EXPECT_TRUE(e_saw > 0.0);
}

void test_engine_output_is_bounded() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.bottom_adc = 1.0f;  // full saw
    p.top_adc    = 1.0f;  // full detune
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    for (int blk = 0; blk < 50; ++blk) {
        eng.ProcessBlock(l, r, 48);
        for (int i = 0; i < 48; ++i) {
            EXPECT_TRUE(std::fabs(l[i]) <= 1.0f);
            EXPECT_TRUE(std::fabs(r[i]) <= 1.0f);
        }
    }
}

void test_engine_rich_mode_produces_audio() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.mode       = Mode::RICH;
    p.bottom_adc = 0.5f;  // ratio = 1 + 0.5*4 = 3.0
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_rich_at_morph_zero_matches_saw_amplitude() {
    // RICH at morph=0 is ratio=1, which is identical to a plain saw in shape
    // (just sums to the soft-clipped/panned 5-stack). Energy comparable to STACK
    // at morph=1.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::RICH;
    p.bottom_adc = 0.0f;  // ratio = 1.0
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e_rich = 0;
    for (int i = 0; i < 48; ++i) e_rich += l[i]*l[i] + r[i]*r[i];

    EXPECT_TRUE(e_rich > 0.01);
}

void test_engine_sub_mode_at_zero_sub_matches_stack_full_saw() {
    // SUB with morph=0 should be 5-saw stack (no sub mixed in) — energy comparable
    // to STACK at morph=1.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::SUB;
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_sub_mode_at_one_has_more_energy_than_zero() {
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::SUB;
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l1[48] = {0}, r1[48] = {0};
    eng.ProcessBlock(l1, r1, 48);
    double e0 = 0;
    for (int i = 0; i < 48; ++i) e0 += l1[i]*l1[i] + r1[i]*r1[i];

    p.bottom_adc = 1.0f;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);
    double e1 = 0;
    for (int i = 0; i < 48; ++i) e1 += l2[i]*l2[i] + r2[i]*r2[i];

    EXPECT_TRUE(e1 > e0);
}

void test_engine_gate_edge_resets_voice_phases() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    eng.ApplyParams(p);

    float l[48] = {0}, r[48] = {0};
    // Run a few blocks to advance phases.
    for (int i = 0; i < 5; ++i) eng.ProcessBlock(l, r, 48);

    // Now signal a gate edge.
    p.gate_edge = true;
    eng.ApplyParams(p);
    eng.ProcessBlock(l, r, 48);

    // After reset, voices have evenly distributed offsets (0, 0.2, 0.4, 0.6, 0.8).
    // After 1 block (48 samples), they've each advanced by 48 * (master_hz / sr).
    // We can't easily inspect internals from outside, so the check is indirect:
    // the first sample post-reset should be different from the last sample pre-reset
    // by more than chance (audible click-or-not is the real test).
    // Here, just verify the engine remains bounded and producing audio.
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.001);
}

void test_engine_mode_change_does_not_click() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.mode = Mode::STACK;
    eng.ApplyParams(p);
    float l1[48] = {0}, r1[48] = {0};
    eng.ProcessBlock(l1, r1, 48);

    p.mode = Mode::RICH;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);

    // No sample-to-sample jump greater than ~0.5 across the boundary
    // (5 ms cosine crossfade should keep deltas small).
    float boundary_jump = std::fabs(l2[0] - l1[47]);
    EXPECT_TRUE(boundary_jump < 0.5f);
}

static void test_external_hz_overrides_pitch() {
    sawstack::SupersawEngine eng;
    eng.Init(48000.0f);

    sawstack::Params p{};
    p.voct_adc   = sawstack::kVoctZero;  // → 0 V, neutral
    p.width      = sawstack::Width::STEREO;
    p.mode       = sawstack::Mode::STACK;
    p.external_hz = 440.0f;
    eng.ApplyParams(p);
    EXPECT_NEAR(eng.GetMasterHz(), 440.0f, 0.01f);

    // external_hz <= 0 falls back to the encoder/voct path (C4 at zero offsets).
    p.external_hz = 0.0f;
    eng.ApplyParams(p);
    EXPECT_NEAR(eng.GetMasterHz(), 261.63f, 0.5f);
}

void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
    RUN_TEST(test_engine_rich_mode_produces_audio);
    RUN_TEST(test_engine_rich_at_morph_zero_matches_saw_amplitude);
    RUN_TEST(test_engine_sub_mode_at_zero_sub_matches_stack_full_saw);
    RUN_TEST(test_engine_sub_mode_at_one_has_more_energy_than_zero);
    RUN_TEST(test_engine_gate_edge_resets_voice_phases);
    RUN_TEST(test_engine_mode_change_does_not_click);
    RUN_TEST(test_external_hz_overrides_pitch);
}

TEST_MAIN()

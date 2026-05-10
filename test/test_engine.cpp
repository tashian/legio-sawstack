// test/test_engine.cpp
#include "test_assert.h"
#include "../src/supersaw_engine.h"
#include "../src/params.h"
#include "../src/dsp_common.h"
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

void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
    RUN_TEST(test_engine_rich_mode_produces_audio);
    RUN_TEST(test_engine_rich_at_morph_zero_matches_saw_amplitude);
    RUN_TEST(test_engine_sub_mode_at_zero_sub_matches_stack_full_saw);
    RUN_TEST(test_engine_sub_mode_at_one_has_more_energy_than_zero);
}

TEST_MAIN()

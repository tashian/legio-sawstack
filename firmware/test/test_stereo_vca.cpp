// test/test_stereo_vca.cpp
#include "test_assert.h"
#include "../src/stereo_vca.h"
#include "../src/params.h"
#include <cmath>
#include <initializer_list>

using sawstack::MixVoices;
using sawstack::Width;

void test_mono_sums_voices_equally_to_l_and_r() {
    float voices[5] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    float l = 0, r = 0;
    MixVoices(voices, Width::MONO, &l, &r);
    EXPECT_NEAR(l, r, 1e-6);
    // Mean ~0.3; equal-power mono should preserve total energy roughly.
    EXPECT_TRUE(std::fabs(l) > 0.1f);
}

void test_center_voice_is_centered_at_all_widths() {
    // Only voice 0 (center) is non-zero.
    float voices[5] = { 1.0f, 0, 0, 0, 0 };
    for (Width w : {Width::MONO, Width::STEREO, Width::WIDE}) {
        float l = 0, r = 0;
        MixVoices(voices, w, &l, &r);
        EXPECT_NEAR(l, r, 1e-6);  // center voice always equal L/R
    }
}

// Pitch order is voice 4 (lowest) → 2 → 0 → 1 → 3 (highest).
// In WIDE, voice 4 should land hard-left and voice 3 hard-right.
void test_wide_voice_4_pans_hard_left() {
    float voices[5] = { 0, 0, 0, 0, 1.0f };
    float l = 0, r = 0;
    MixVoices(voices, Width::WIDE, &l, &r);
    EXPECT_TRUE(l > 0.9f);
    EXPECT_TRUE(r < 0.1f);
}

void test_wide_voice_3_pans_hard_right() {
    float voices[5] = { 0, 0, 0, 1.0f, 0 };
    float l = 0, r = 0;
    MixVoices(voices, Width::WIDE, &l, &r);
    EXPECT_TRUE(r > 0.9f);
    EXPECT_TRUE(l < 0.1f);
}

void test_stereo_pans_less_than_wide() {
    float voices[5] = { 0, 0, 0, 0, 1.0f };  // voice 4: lowest pitch, leftmost
    float l_stereo = 0, r_stereo = 0;
    float l_wide   = 0, r_wide   = 0;
    MixVoices(voices, Width::STEREO, &l_stereo, &r_stereo);
    MixVoices(voices, Width::WIDE,   &l_wide,   &r_wide);
    // Both pan left; WIDE pans further (more L, less R).
    EXPECT_TRUE(l_wide > l_stereo);
    EXPECT_TRUE(r_wide < r_stereo);
}

void run_all() {
    RUN_TEST(test_mono_sums_voices_equally_to_l_and_r);
    RUN_TEST(test_center_voice_is_centered_at_all_widths);
    RUN_TEST(test_wide_voice_4_pans_hard_left);
    RUN_TEST(test_wide_voice_3_pans_hard_right);
    RUN_TEST(test_stereo_pans_less_than_wide);
}

TEST_MAIN()

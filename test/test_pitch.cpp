// test/test_pitch.cpp
#include "test_assert.h"
#include "../src/pitch.h"
#include <cmath>

using sawstack::ComputeMasterHz;
using sawstack::VoctVoltsFromAdc;
#include "../src/dsp_common.h"  // for kNumVoices
using sawstack::ComputeVoiceFreqs;

void test_master_hz_at_zero_is_c4() {
    EXPECT_NEAR(ComputeMasterHz(0, 0, 0.0f), 261.63f, 0.01f);
}

void test_master_hz_octave_up_doubles() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(12, 0, 0.0f);
    EXPECT_NEAR(b, a * 2.0f, 0.05f);
}

void test_master_hz_voct_one_volt_doubles() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(0, 0, 1.0f);
    EXPECT_NEAR(b, a * 2.0f, 0.05f);
}

void test_master_hz_fine_50_cents_up() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(0, 50, 0.0f);  // +50 cents = ratio 2^(50/1200)
    EXPECT_NEAR(b / a, 1.0293f, 0.001f);  // 2^(1/24) ≈ 1.0293
}

// kVoctScale=0 means the v/oct path is bypassed: any ADC value yields 0V.
void test_voct_disabled_when_scale_zero() {
    EXPECT_NEAR(VoctVoltsFromAdc(0.5f, /*zero=*/0.123f, /*scale=*/0.0f), 0.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.9f, /*zero=*/0.123f, /*scale=*/0.0f), 0.0f, 1e-6);
}

void test_voct_with_calibrated_constants() {
    // Suppose 0V → adc 0.20, +1V → adc 0.40. Then scale = 1/(0.40-0.20) = 5.
    float zero = 0.20f, scale = 5.0f;
    EXPECT_NEAR(VoctVoltsFromAdc(0.20f, zero, scale), 0.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.40f, zero, scale), 1.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.60f, zero, scale), 2.0f, 1e-6);
}

void test_voice_freqs_unison_at_detune_zero() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/0.0f, freqs);
    for (int i = 0; i < 5; ++i) EXPECT_NEAR(freqs[i], 440.0f, 1e-3);
}

void test_voice_freqs_voice0_always_at_master() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    EXPECT_NEAR(freqs[0], 440.0f, 1e-3);
}

void test_voice_freqs_two_up_two_down_at_full() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voices 1 and 3 are positive offsets (above master); 2 and 4 negative.
    EXPECT_TRUE(freqs[1] > 440.0f);
    EXPECT_TRUE(freqs[3] > freqs[1]);
    EXPECT_TRUE(freqs[2] < 440.0f);
    EXPECT_TRUE(freqs[4] < freqs[2]);
}

void test_voice_freqs_full_spread_is_2_octaves() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voice 3 (highest) should be ~+24 semitones ≈ 4× master, ±5%.
    // Spread is detune_norm² * 2400 cents = 2400 cents = 2 octaves
    // applied with multiplier 1.013 → 2.026 octaves up → ~4.07× master.
    EXPECT_NEAR(freqs[3] / 440.0f, 4.07f, 0.1f);
    EXPECT_NEAR(440.0f / freqs[4], 4.06f, 0.1f);  // voice 4 is symmetric down (mult 1.011)
}

void test_voice_freqs_asymmetric_multipliers_break_octave_coincidence() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voice 3 is at 1.0 × 1.013 × 2400 cents = 2431.2 cents.
    // Pure octave would be exactly 2400 (4×). We want it slightly off.
    EXPECT_TRUE(std::fabs(freqs[3] - 4.0f * 440.0f) > 5.0f);
}

void run_all() {
    RUN_TEST(test_master_hz_at_zero_is_c4);
    RUN_TEST(test_master_hz_octave_up_doubles);
    RUN_TEST(test_master_hz_voct_one_volt_doubles);
    RUN_TEST(test_master_hz_fine_50_cents_up);
    RUN_TEST(test_voct_disabled_when_scale_zero);
    RUN_TEST(test_voct_with_calibrated_constants);
    RUN_TEST(test_voice_freqs_unison_at_detune_zero);
    RUN_TEST(test_voice_freqs_voice0_always_at_master);
    RUN_TEST(test_voice_freqs_two_up_two_down_at_full);
    RUN_TEST(test_voice_freqs_full_spread_is_2_octaves);
    RUN_TEST(test_voice_freqs_asymmetric_multipliers_break_octave_coincidence);
}

TEST_MAIN()

// test/test_pitch.cpp
#include "test_assert.h"
#include "../src/pitch.h"

using sawstack::ComputeMasterHz;
using sawstack::VoctVoltsFromAdc;

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

void run_all() {
    RUN_TEST(test_master_hz_at_zero_is_c4);
    RUN_TEST(test_master_hz_octave_up_doubles);
    RUN_TEST(test_master_hz_voct_one_volt_doubles);
    RUN_TEST(test_master_hz_fine_50_cents_up);
    RUN_TEST(test_voct_disabled_when_scale_zero);
    RUN_TEST(test_voct_with_calibrated_constants);
}

TEST_MAIN()

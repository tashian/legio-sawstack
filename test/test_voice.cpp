// test/test_voice.cpp
#include "test_assert.h"
#include "../src/voice.h"
#include "../src/dsp_common.h"

using sawstack::Voice;
using sawstack::kSampleRate;

// Naive saw at 1000 Hz should ramp from -1 toward +1 over (sr / freq) samples.
void test_naive_saw_ramps() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(1000.0f);

    // Naive saw output is phase * 2 - 1, where phase wraps in [0, 1).
    // First sample: phase=0 → -1.0
    EXPECT_NEAR(v.NaiveSaw(), -1.0f, 1e-5);
    // After advancing one sample at 1000 Hz / 48 kHz, phase = 1000/48000.
    EXPECT_NEAR(v.NaiveSaw(), -1.0f + 2.0f * (1000.0f / 48000.0f), 1e-5);
}

void test_naive_saw_wraps() {
    Voice v;
    v.Init(kSampleRate);
    // 1500 Hz → phase_inc = 1/32 exactly in float; period = 32 samples.
    v.SetFrequency(1500.0f);

    // Run for one full period (32 samples), output should be exactly -1 again.
    for (int i = 0; i < 32; ++i) v.NaiveSaw();
    EXPECT_NEAR(v.NaiveSaw(), -1.0f, 1e-3);
}

// PolyBLEP saw should still ramp like a saw on average.
void test_polyblep_saw_dc_offset_low() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(1000.0f);

    // Average a full period; expect near zero (saw is symmetric around zero).
    double sum = 0.0;
    int n = 480;  // 10 periods
    for (int i = 0; i < n; ++i) sum += v.Saw();
    EXPECT_NEAR(sum / n, 0.0, 0.01);
}

// At low frequency, polyBLEP correction is negligible — output ≈ naive saw.
void test_polyblep_matches_naive_at_low_freq() {
    Voice naive, blep;
    naive.Init(kSampleRate);
    blep.Init(kSampleRate);
    naive.SetFrequency(50.0f);
    blep.SetFrequency(50.0f);

    // Sample halfway through the cycle, far from the discontinuity.
    for (int i = 0; i < 480; ++i) {  // jump to mid-period (50 Hz / 48k = 480 samp / period)
        naive.NaiveSaw();
        blep.Saw();
    }
    // Now phases are aligned at ~0.5; correction is zero there.
    EXPECT_NEAR(naive.NaiveSaw(), blep.Saw(), 0.01);
}

void test_sine_at_quarter_period_is_one() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(1.0f);  // 1 Hz: one period = 48000 samples; quarter = 12000.

    for (int i = 0; i < 12000; ++i) v.Sine();
    EXPECT_NEAR(v.Sine(), 1.0f, 1e-2);
}

void test_morph_at_zero_is_sine_at_one_is_saw() {
    Voice vs, vm;
    vs.Init(kSampleRate);
    vm.Init(kSampleRate);
    vs.SetFrequency(440.0f);
    vm.SetFrequency(440.0f);

    // morph=0 → pure sine
    EXPECT_NEAR(vs.Sine(), vm.Morph(0.0f), 1e-5);
    // re-init for the saw arm
    vs.Init(kSampleRate); vm.Init(kSampleRate);
    vs.SetFrequency(440.0f); vm.SetFrequency(440.0f);
    // morph=1 → pure saw
    EXPECT_NEAR(vs.Saw(), vm.Morph(1.0f), 1e-5);
}

void test_phase_reset_zeros_phase() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(1000.0f);

    // Advance arbitrarily.
    for (int i = 0; i < 17; ++i) v.Saw();
    v.ResetPhase(0.4f);
    // After reset, NaiveSaw output should be 0.4*2-1 = -0.2 immediately.
    EXPECT_NEAR(v.NaiveSaw(), -0.2f, 1e-5);
}

// At richness=1 (ratio=1) the slave phase tracks the master exactly,
// so output should match a plain saw of the same frequency.
void test_hardsync_at_ratio_one_matches_saw() {
    Voice plain, sync;
    plain.Init(kSampleRate);
    sync.Init(kSampleRate);
    plain.SetFrequency(440.0f);
    sync.SetFrequency(440.0f);

    for (int i = 0; i < 100; ++i) {
        EXPECT_NEAR(plain.Saw(), sync.HardSync(1.0f), 1e-4);
    }
}

// At ratio>1, the slave wraps multiple times per master period.
// Sum over one master period should be ~0 (still saw-symmetric).
void test_hardsync_dc_offset_low_at_ratio_three() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(440.0f);

    double sum = 0.0;
    int n = static_cast<int>(kSampleRate / 440.0f) * 4;  // ~4 master periods
    for (int i = 0; i < n; ++i) sum += v.HardSync(3.0f);
    EXPECT_NEAR(sum / n, 0.0, 0.05);
}

// Reset must zero both master and slave phases — verified by comparing against
// a fresh Voice that was never advanced (both should produce identical output).
void test_hardsync_phase_reset_zeros_both() {
    Voice baseline;
    baseline.Init(kSampleRate);
    baseline.SetFrequency(440.0f);

    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(440.0f);
    for (int i = 0; i < 23; ++i) v.HardSync(2.5f);
    v.ResetPhase(0.0f);
    v.ResetSyncPhase();
    EXPECT_NEAR(v.HardSync(1.0f), baseline.HardSync(1.0f), 1e-5);
}

void run_all() {
    RUN_TEST(test_naive_saw_ramps);
    RUN_TEST(test_naive_saw_wraps);
    RUN_TEST(test_polyblep_saw_dc_offset_low);
    RUN_TEST(test_polyblep_matches_naive_at_low_freq);
    RUN_TEST(test_sine_at_quarter_period_is_one);
    RUN_TEST(test_morph_at_zero_is_sine_at_one_is_saw);
    RUN_TEST(test_phase_reset_zeros_phase);
    RUN_TEST(test_hardsync_at_ratio_one_matches_saw);
    RUN_TEST(test_hardsync_dc_offset_low_at_ratio_three);
    RUN_TEST(test_hardsync_phase_reset_zeros_both);
}

TEST_MAIN()

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

void run_all() {
    RUN_TEST(test_naive_saw_ramps);
    RUN_TEST(test_naive_saw_wraps);
    RUN_TEST(test_polyblep_saw_dc_offset_low);
    RUN_TEST(test_polyblep_matches_naive_at_low_freq);
}

TEST_MAIN()

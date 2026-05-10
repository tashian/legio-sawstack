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
    v.SetFrequency(1000.0f);

    // Run for one full period (48 samples), output should be near -1 again.
    for (int i = 0; i < 48; ++i) v.NaiveSaw();
    EXPECT_NEAR(v.NaiveSaw(), -1.0f, 1e-3);
}

void run_all() {
    RUN_TEST(test_naive_saw_ramps);
    RUN_TEST(test_naive_saw_wraps);
}

TEST_MAIN()

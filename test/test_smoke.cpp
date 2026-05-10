// test/test_smoke.cpp — verifies the harness itself compiles and links.
#include "test_assert.h"
#include "../src/dsp_common.h"
#include "../src/params.h"
#include "../src/soft_clip.h"
#include "../src/leds.h"
using sawstack::ComputeLeds;
using sawstack::LedInputs;
using sawstack::Rgb;

void test_constants() {
    EXPECT_EQ(sawstack::kAudioBlockSize, 48);
    EXPECT_EQ(sawstack::kNumVoices, 5);
}

void test_soft_clip_passes_unity() {
    EXPECT_NEAR(sawstack::soft_clip(0.0f), 0.0f, 1e-6);
    EXPECT_NEAR(sawstack::soft_clip(0.5f), 0.5f - 0.5f*0.5f*0.5f/3.0f, 1e-6);
    EXPECT_NEAR(sawstack::soft_clip(2.0f), 2.0f / 3.0f, 1e-6);
    EXPECT_NEAR(sawstack::soft_clip(-2.0f), -2.0f / 3.0f, 1e-6);
}

void test_leds_boot_pattern_alternates() {
    LedInputs in;
    in.mode = sawstack::Mode::STACK; in.width = sawstack::Width::STEREO;
    in.last_gate_edge_ms = 0;
    in.boot_ms = 0;          in.now_ms = 0;
    Rgb a_l, a_r; ComputeLeds(in, &a_l, &a_r);

    in.boot_ms = 125;        in.now_ms = 125;
    Rgb b_l, b_r; ComputeLeds(in, &b_l, &b_r);
    // The left LED should differ between phase 0 and phase 1 of the boot blink.
    EXPECT_TRUE(a_l.r != b_l.r || a_l.g != b_l.g || a_l.b != b_l.b);
}

void test_leds_wide_brighter_than_stereo() {
    LedInputs in;
    in.mode = sawstack::Mode::STACK;
    in.last_gate_edge_ms = 0;
    in.boot_ms = 600;        in.now_ms = 600;
    in.width = sawstack::Width::STEREO;
    Rgb sl, sr; ComputeLeds(in, &sl, &sr);
    in.width = sawstack::Width::WIDE;
    Rgb wl, wr; ComputeLeds(in, &wl, &wr);
    EXPECT_TRUE(wr.b > sr.b);
}

void run_all() {
    RUN_TEST(test_constants);
    RUN_TEST(test_soft_clip_passes_unity);
    RUN_TEST(test_leds_boot_pattern_alternates);
    RUN_TEST(test_leds_wide_brighter_than_stereo);
}

TEST_MAIN()

// test/test_smoke.cpp — verifies the harness itself compiles and links.
#include "test_assert.h"
#include "../src/dsp_common.h"
#include "../src/params.h"
#include "../src/soft_clip.h"

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

void run_all() {
    RUN_TEST(test_constants);
    RUN_TEST(test_soft_clip_passes_unity);
}

TEST_MAIN()

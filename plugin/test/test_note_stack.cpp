#include "note_stack.h"
#include "test_assert.h"

using sawstack::NoteStack;
using sawstack::NoteToHz;

static void test_empty_stack() {
    NoteStack s;
    EXPECT_TRUE(!s.HasNote());
    EXPECT_EQ(s.ActiveNote(), -1);
}

static void test_last_note_priority() {
    NoteStack s;
    s.NoteOn(60);
    EXPECT_EQ(s.ActiveNote(), 60);
    s.NoteOn(64);
    EXPECT_EQ(s.ActiveNote(), 64);   // newest wins
    s.NoteOn(67);
    EXPECT_EQ(s.ActiveNote(), 67);
}

static void test_release_falls_back() {
    NoteStack s;
    s.NoteOn(60);
    s.NoteOn(64);
    s.NoteOn(67);
    s.NoteOff(67);
    EXPECT_EQ(s.ActiveNote(), 64);   // back to previous held
    s.NoteOff(60);                   // releasing a non-active note
    EXPECT_EQ(s.ActiveNote(), 64);   // active unaffected
    s.NoteOff(64);
    EXPECT_TRUE(!s.HasNote());
    EXPECT_EQ(s.ActiveNote(), -1);
}

static void test_note_to_hz() {
    EXPECT_NEAR(NoteToHz(69, 0.0f, 0, 0.0f), 440.0f, 0.01f);     // A4
    EXPECT_NEAR(NoteToHz(60, 0.0f, 0, 0.0f), 261.6256f, 0.01f);  // C4
    EXPECT_NEAR(NoteToHz(69, 2.0f, 0, 0.0f), 493.8833f, 0.01f);  // +2 st bend → B4
    EXPECT_NEAR(NoteToHz(57, 0.0f, 12, 0.0f), 440.0f, 0.01f);    // A3 + 12 st coarse → A4
    EXPECT_NEAR(NoteToHz(69, 0.0f, 0, 100.0f), 466.1638f, 0.01f);// +100 cents → A#4
}

void run_all() {
    RUN_TEST(test_empty_stack);
    RUN_TEST(test_last_note_priority);
    RUN_TEST(test_release_falls_back);
    RUN_TEST(test_note_to_hz);
}

TEST_MAIN()

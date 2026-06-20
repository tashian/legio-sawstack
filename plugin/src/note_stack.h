// plugin/src/note_stack.h
#pragma once
#include <vector>

namespace sawstack {

// Monophonic last-note-priority note tracker. JUCE-independent and host-tested.
class NoteStack {
  public:
    void NoteOn(int note);    // push; becomes active (re-press moves to active)
    void NoteOff(int note);   // remove anywhere; active = most-recent remaining
    bool HasNote() const { return !stack_.empty(); }
    int  ActiveNote() const { return stack_.empty() ? -1 : stack_.back(); }

  private:
    std::vector<int> stack_;  // arrival order; back() is the active note
};

// MIDI note (+ pitch-bend semitones, coarse semitones, fine cents) → frequency Hz.
float NoteToHz(int midiNote, float bendSemitones, int coarseSemitones, float fineCents);

}  // namespace sawstack

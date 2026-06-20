// plugin/src/note_stack.cpp
#include "note_stack.h"
#include <algorithm>
#include <cmath>

namespace sawstack {

void NoteStack::NoteOn(int note) {
    // Remove any existing instance so a re-press moves the note to active (back).
    stack_.erase(std::remove(stack_.begin(), stack_.end(), note), stack_.end());
    stack_.push_back(note);
}

void NoteStack::NoteOff(int note) {
    stack_.erase(std::remove(stack_.begin(), stack_.end(), note), stack_.end());
}

float NoteToHz(int midiNote, float bendSemitones, int coarseSemitones, float fineCents) {
    const float semis = static_cast<float>(midiNote - 69)
                      + bendSemitones
                      + static_cast<float>(coarseSemitones)
                      + fineCents / 100.0f;
    return 440.0f * std::pow(2.0f, semis / 12.0f);
}

}  // namespace sawstack

// src/main.cpp — placeholder until Task 13 wires the engine.
#include "daisy_legio.h"

using namespace daisy;

DaisyLegio hw;

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t size) {
    for (size_t i = 0; i < size; i += 2) {
        out[i + 0] = 0.0f;
        out[i + 1] = 0.0f;
    }
}

int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(48);
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while (true) { System::Delay(100); }
}

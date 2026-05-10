// src/dsp_common.h
#pragma once

namespace sawstack {

constexpr float kSampleRate     = 48000.0f;
constexpr int   kAudioBlockSize = 48; // samples per channel per callback
constexpr int   kNumVoices      = 5;  // 2 down, 1 center, 2 up

}  // namespace sawstack

// On host builds the Daisy SDRAM section attribute doesn't exist; stub it.
#ifndef DSY_SDRAM_BSS
#  define DSY_SDRAM_BSS
#endif

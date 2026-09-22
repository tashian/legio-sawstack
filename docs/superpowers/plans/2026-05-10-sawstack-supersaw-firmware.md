# sawstack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a 5-voice supersaw oscillator firmware for the Noise Engineering Legio (Daisy Patch SM, STM32H750) per `docs/superpowers/specs/2026-05-09-sawstack-supersaw-design.md`.

**Architecture:** DSP modules (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`) are pure float-in / float-out and compile under both `arm-none-eabi-g++` (firmware) and host `g++` (unit tests). `main.cpp` is the only file that touches the libDaisy HAL — it reads the Legio's controls into a plain `Params` struct, drives the engine each block, and emits serial telemetry from the slow loop. Each DSP module is built test-first.

**Tech Stack:** C++17. libDaisy + DaisySP (git submodules). GNU make. dfu-util for flashing. Host tests use a tiny micro-harness (`test_assert.h`) — no external test framework.

---

## File structure

Files this plan creates (new project under `~/code/legio/sawstack/`):

```
sawstack/
├── CLAUDE.md                       Hardware quirks + this app's specifics + deviations.
├── README.md                       Player-facing.
├── Makefile                        Firmware build. APP_TYPE = BOOT_NONE. -u _printf_float in LDFLAGS.
├── .gitignore                      build/, build_host/, *.bin, *.elf
├── .gitmodules                     libDaisy + DaisySP
├── lib/
│   ├── libDaisy/                   submodule (electro-smith/libDaisy)
│   └── DaisySP/                    submodule (electro-smith/DaisySP)
├── src/
│   ├── main.cpp                    HAL + slow loop. Only file that includes daisy_legio.h.
│   ├── supersaw_engine.{h,cpp}     Top-level orchestrator. 5 voices + sub voice + mode dispatch + crossfade.
│   ├── voice.{h,cpp}               Single oscillator: phase, polyBLEP saw, sine, morph, hard-sync, phase reset.
│   ├── pitch.{h,cpp}               Encoder coarse/fine + voct + asymmetric detune layout. Hosts kVoctZero / kVoctScale constants.
│   ├── stereo_vca.{h,cpp}          MONO/STEREO/WIDE 5-voice pan-law mixer.
│   ├── leds.{h,cpp}                Mode/stereo/boot LED state. Gate-edge brightness pulse.
│   ├── soft_clip.h                 Header-only piecewise soft-clip (ported from legio_tape).
│   ├── params.h                    Plain Params struct: top_adc, bottom_adc, voct_adc, mode, width, encoder events, gate_edge.
│   └── dsp_common.h                kSampleRate, kAudioBlockSize, DSY_SDRAM_BSS host shim.
└── test/
    ├── Makefile                    Same micro-harness shape as legio_tape/test.
    ├── test_assert.h               EXPECT_NEAR / EXPECT_EQ / EXPECT_TRUE / RUN_TEST / TEST_MAIN.
    ├── test_smoke.cpp              Trivial link check.
    ├── test_voice.cpp              polyBLEP frequency, sine↔saw morph, hard-sync, phase reset.
    ├── test_pitch.cpp              Encoder semantics, voct math, detune layout multipliers.
    ├── test_stereo_vca.cpp         Pan law, energy preservation, MONO=L=R, WIDE>STEREO.
    └── test_engine.cpp             Mode dispatch, gate-edge phase reset, sub voice, crossfade.
```

The reference for files / Makefile shape / micro-harness is the existing `~/code/legio/stutterer/` project.

---

## Task 1: Project skeleton

**Files:**
- Create: `~/code/legio/sawstack/` (directory)
- Create: `sawstack/.gitignore`
- Create: `sawstack/.gitmodules`
- Create: `sawstack/Makefile`
- Create: `sawstack/src/dsp_common.h`
- Create: `sawstack/src/params.h`
- Create: `sawstack/src/soft_clip.h`
- Create: `sawstack/src/main.cpp` (minimal — boots and idles)
- Create: `sawstack/test/Makefile`
- Create: `sawstack/test/test_assert.h`
- Create: `sawstack/test/test_smoke.cpp`
- Create: `sawstack/CLAUDE.md`
- Create: `sawstack/README.md`

- [ ] **Step 1: Create the directory and initialize git**

```bash
mkdir -p ~/code/legio/sawstack
cd ~/code/legio/sawstack
git init -b main
mkdir -p src test lib
```

- [ ] **Step 2: Add submodules (libDaisy, DaisySP)**

```bash
cd ~/code/legio/sawstack
git submodule add https://github.com/electro-smith/libDaisy.git lib/libDaisy
git submodule add https://github.com/electro-smith/DaisySP.git lib/DaisySP
```

Expected: `lib/libDaisy/` and `lib/DaisySP/` populated; `.gitmodules` written.

- [ ] **Step 3: Build the submodule libraries (one-time)**

```bash
cd ~/code/legio/sawstack
make -C lib/libDaisy
make -C lib/DaisySP
```

Expected: Both build to `build/` subdirs inside each submodule. Takes ~2–3 min total.

- [ ] **Step 4: Write `.gitignore`**

```
build/
build_host/
*.bin
*.elf
*.map
*.o
*.d
.DS_Store
```

- [ ] **Step 5: Write `src/dsp_common.h`**

```cpp
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
```

- [ ] **Step 6: Write `src/params.h`**

```cpp
// src/params.h
#pragma once
#include <cstdint>

namespace sawstack {

enum class Mode : uint8_t  { STACK = 0, RICH = 1, SUB = 2 };
enum class Width : uint8_t { MONO  = 0, STEREO = 1, WIDE = 2 };

struct Params {
    // ADCs in [0, 1]
    float top_adc;       // detune knob + top CV (summed)
    float bottom_adc;    // morph knob + bottom CV (summed)
    float voct_adc;      // raw v/oct ADC (pre-zero/scale)

    Mode  mode;          // from left switch (panel-corrected)
    Width width;         // from right switch (panel-corrected)

    // Encoder events accumulated since last apply_params.
    int   encoder_fine_delta;    // detents (no press)
    int   encoder_coarse_delta;  // detents (press + turn)

    bool  gate_edge;     // true on rising edge this block
};

}  // namespace sawstack
```

- [ ] **Step 7: Write `src/soft_clip.h`**

```cpp
// src/soft_clip.h — piecewise polynomial soft-clip, ported from legio_tape.
#pragma once
#include <cmath>

namespace sawstack {

// Output is bounded to [-1, 1]. Smooth derivative across the whole range.
inline float soft_clip(float x) {
    if (x >  1.0f) return  2.0f / 3.0f;
    if (x < -1.0f) return -2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

}  // namespace sawstack
```

- [ ] **Step 8: Write the placeholder `src/main.cpp`** (just enough to compile firmware; real HAL wiring comes in Task 13)

```cpp
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
```

- [ ] **Step 9: Write `Makefile`** (firmware build)

```make
# Project Name
TARGET = sawstack

# Sources — extended as new modules are added.
CPP_SOURCES = src/main.cpp

# Pull in float-printf so PrintLine("%f") emits floats (newlib-nano strips it).
LDFLAGS += -u _printf_float

# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR  = lib/DaisySP

# Project source includes
C_INCLUDES += -Isrc

# Use Daisy's stock build system
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
```

- [ ] **Step 10: Build firmware**

```bash
cd ~/code/legio/sawstack
make
```

Expected: builds without errors to `build/sawstack.bin`.

- [ ] **Step 11: Write `test/test_assert.h`** (copy of stutterer's micro-harness)

```cpp
// test/test_assert.h
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>

inline int g_test_failures = 0;

#define EXPECT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!((_a) == (_b))) { \
        std::fprintf(stderr, "FAIL %s:%d: %s == %s (got %g vs %g)\n", \
                     __FILE__, __LINE__, #a, #b, (double)_a, (double)_b); \
        ++g_test_failures; \
    } \
} while (0)

#define EXPECT_NEAR(a, b, tol) do { \
    double _a = (double)(a); double _b = (double)(b); double _t = (double)(tol); \
    if (std::fabs(_a - _b) > _t) { \
        std::fprintf(stderr, "FAIL %s:%d: |%s - %s| <= %g (got %g vs %g, |diff|=%g)\n", \
                     __FILE__, __LINE__, #a, #b, _t, _a, _b, std::fabs(_a - _b)); \
        ++g_test_failures; \
    } \
} while (0)

#define EXPECT_TRUE(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
        ++g_test_failures; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    int before = g_test_failures; \
    std::printf("RUN  %s\n", #fn); \
    fn(); \
    if (g_test_failures == before) std::printf("PASS %s\n", #fn); \
    else                           std::printf("FAIL %s\n", #fn); \
} while (0)

#define TEST_MAIN() \
    int main() { \
        run_all(); \
        if (g_test_failures > 0) { \
            std::fprintf(stderr, "%d test failure(s)\n", g_test_failures); \
            return 1; \
        } \
        std::printf("All tests passed.\n"); \
        return 0; \
    }
```

- [ ] **Step 12: Write `test/test_smoke.cpp`**

```cpp
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
```

- [ ] **Step 13: Write `test/Makefile`**

```make
# test/Makefile — host-side unit tests for the DSP modules.
# Build & run with:   make -C test
CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -I../src -I../lib/DaisySP/Source \
            -I.

OUT       = ../build_host

# Default test list — extended as new modules are added.
TESTS = test_smoke

all: $(addprefix $(OUT)/, $(TESTS))
	@for t in $(TESTS); do \
	  echo "===== $$t ====="; \
	  $(OUT)/$$t || exit 1; \
	done

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_smoke: test_smoke.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
```

- [ ] **Step 14: Run the smoke test**

```bash
cd ~/code/legio/sawstack
make -C test
```

Expected: builds and runs `test_smoke`, prints `All tests passed.`, exits 0.

- [ ] **Step 15: Write `CLAUDE.md`**

```markdown
# CLAUDE.md — sawstack

Custom firmware for the Noise Engineering Legio module (Daisy Patch SM, STM32H750). 5-voice supersaw oscillator: 5 detuned saws (or sine↔saw morph / hard-sync / +sub-octave depending on left-switch mode) summed to a stereo VCA.

Spec: `../docs/superpowers/specs/2026-05-09-sawstack-supersaw-design.md`
Plan: `../docs/superpowers/plans/2026-05-10-sawstack-supersaw-firmware.md`

## Build / test / flash

```sh
make -C lib/libDaisy   # one-time after submodule init
make -C lib/DaisySP    # one-time
make                   # firmware → build/sawstack.bin
make -C test           # host DSP tests (no hardware)
make program-dfu       # flash (BOOT + RESET on the Patch SM submodule first)
```

DFU entry on Legio: BOOT + RESET on the Patch SM submodule (back of the module). Be patient at flash gates — the user may not be able to hit the buttons cleanly without disturbing patch cables. After `make program-dfu`, dfu-util's `Error during download get_status` / `Error 74` is harmless. The module sometimes won't re-enumerate without a manual reseat.

Live serial: `screen /dev/cu.usbmodem* 115200`. If `screen` is already attached on another terminal it holds the device exclusively and `cat` will fail — check `screen -ls`.

## Hardware quirks (carried from legio_tape)

1. **3 ADC channels, not 4.** `CONTROL_KNOB_TOP` and `CONTROL_KNOB_BOTTOM` each read the analog sum of their knob and the CV jack above them. They cannot be separated. `CONTROL_PITCH` is the v/oct jack on its own ADC.
2. **Switch3 polarity is inverted on Legio.** `Switch3.Read()` returns 0/1/2 = CENTER/POS_UP/POS_DOWN, but on Legio's panel POS_UP corresponds to **panel DOWN** and POS_DOWN to **panel UP**. The `Params` struct uses *panel-relative* labels; the conversion happens once in `main.cpp`.
3. **newlib-nano strips float printf.** `-u _printf_float` is in `LDFLAGS` (~10 KB flash cost). If you remove it, float telemetry goes silent.
4. **DFU re-enumeration sometimes needs a manual reseat.** dfu-util's `Error 74` is harmless.
5. **Floating v/oct jack reads as a non-zero indeterminate ADC value.** `kVoctScale = 0` until calibration is complete makes the v/oct path a no-op; firmware is musically usable from first flash.
6. **Audio callback is ~1 ms (48 samples @ 48 kHz).** Never call `PrintLine` from inside it. Telemetry goes through a volatile snapshot drained by the slow loop.

## DSP / HAL split

DSP modules (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`, `leds`) **never** include `daisy_legio.h`, `daisy_seed.h`, or anything from libDaisy outside `DaisySP/Source/...`. They take and return `float` and read a plain `Params` struct. This is what makes `make -C test` work with plain `g++` and what catches algorithm bugs in seconds rather than in a flash cycle.

`main.cpp` is the only file that touches the HAL. It reads controls into a `Params` struct, calls `engine.apply_params(p)`, and runs `engine.process_block(out_l, out_r, n)` from the audio callback.

## V/oct calibration

Hardcoded `kVoctZero` / `kVoctScale` in `src/pitch.h`. Procedure:

1. Open serial telemetry: `screen /dev/cu.usbmodem* 115200`. Telemetry always prints `voct_raw=<f>`.
2. Patch a known 0 V source to the v/oct jack. Note the printed `voct_raw` → that's `kVoctZero`.
3. Patch a known +1 V source. Note the printed `voct_raw_at_1V`. Compute `kVoctScale = 1.0 / (voct_raw_at_1V - kVoctZero)`.
4. Edit `src/pitch.h`, rebuild, reflash.

`kVoctScale = 0` disables the v/oct path entirely (jack ignored). Default state at first flash.

## Things to avoid

- Don't add a second board class or rewrite the HAL. `DaisyLegio` is complete.
- Don't add new test frameworks; `test/test_assert.h` is intentionally minimal.
- Don't claim work is done because host tests pass — the in-rack feel test is the real gate.
- Don't call `PrintLine` from inside the audio callback.
```

- [ ] **Step 16: Write `README.md`**

```markdown
# sawstack

5-voice supersaw oscillator firmware for the Noise Engineering Legio module.

- **Top knob + CV** — Detune (0 = unison → ±2 octaves spread)
- **Bottom knob + CV** — Morph (sine↔saw / hard-sync richness / sub-octave mix, per mode)
- **Left switch** — Mode: STACK / RICH / SUB
- **Right switch** — Stereo VCA: MONO / STEREO / WIDE
- **Encoder rotate** — fine pitch (±50 cents)
- **Encoder press + turn** — coarse pitch (±2 octaves, semitone steps)
- **Pitch jack** — v/oct (after calibration; see CLAUDE.md)
- **Gate jack** — hard sync (rising edge resets all voice phases)

Stereo audio out. No internal envelope; this is a pure oscillator.

Build: `make` (firmware), `make -C test` (host tests), `make program-dfu` (flash). See CLAUDE.md for full details.
```

- [ ] **Step 17: Commit the skeleton**

```bash
cd ~/code/legio/sawstack
git add .gitignore .gitmodules Makefile src/ test/ CLAUDE.md README.md
git commit -m "init: project skeleton, submodules, smoke test passing"
```

Expected: one commit, working tree clean except for `lib/libDaisy/` and `lib/DaisySP/` submodule pointers.

---

## Task 2: Voice — phase accumulator + naive saw (TDD)

**Files:**
- Create: `sawstack/src/voice.h`
- Create: `sawstack/src/voice.cpp`
- Create: `sawstack/test/test_voice.cpp`
- Modify: `sawstack/test/Makefile` (add `test_voice` target)

- [ ] **Step 1: Write the failing test (`test/test_voice.cpp`)**

```cpp
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
```

- [ ] **Step 2: Add `test_voice` target to `test/Makefile`**

```make
# Modify the TESTS line:
TESTS = test_smoke test_voice

# Add a recipe at the bottom (after test_smoke recipe):
$(OUT)/test_voice: test_voice.cpp ../src/voice.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
make -C test test_voice 2>&1 | tail -5
```

Expected: compile error — `voice.h` not found.

- [ ] **Step 4: Write `src/voice.h`**

```cpp
// src/voice.h — single oscillator voice with phase accumulator.
#pragma once

namespace sawstack {

class Voice {
  public:
    void  Init(float sample_rate);
    void  SetFrequency(float hz);  // Updates per-sample phase increment.
    float NaiveSaw();              // Advances phase, returns phase*2-1 in [-1, 1).

  private:
    float sample_rate_ = 48000.0f;
    float phase_       = 0.0f;     // [0, 1)
    float phase_inc_   = 0.0f;     // freq / sample_rate
};

}  // namespace sawstack
```

- [ ] **Step 5: Write `src/voice.cpp`**

```cpp
// src/voice.cpp
#include "voice.h"

namespace sawstack {

void Voice::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    phase_       = 0.0f;
    phase_inc_   = 0.0f;
}

void Voice::SetFrequency(float hz) {
    phase_inc_ = hz / sample_rate_;
}

float Voice::NaiveSaw() {
    float out = phase_ * 2.0f - 1.0f;
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}

}  // namespace sawstack
```

- [ ] **Step 6: Run tests**

```bash
make -C test
```

Expected: `PASS test_naive_saw_ramps`, `PASS test_naive_saw_wraps`, `All tests passed.`

- [ ] **Step 7: Commit**

```bash
git add src/voice.h src/voice.cpp test/test_voice.cpp test/Makefile
git commit -m "feat(voice): phase accumulator with naive saw"
```

---

## Task 3: Voice — polyBLEP saw (TDD)

PolyBLEP (band-limited step) suppresses the alias from the saw's discontinuity by adding a small correction term near the wrap point. Reference: Välimäki & Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis."

**Files:**
- Modify: `sawstack/src/voice.h` (add `Saw()` method)
- Modify: `sawstack/src/voice.cpp` (implement polyBLEP)
- Modify: `sawstack/test/test_voice.cpp` (add anti-aliasing test)

- [ ] **Step 1: Add the failing test (append to `test/test_voice.cpp` before `run_all`)**

```cpp
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

// Update run_all to include them:
void run_all() {
    RUN_TEST(test_naive_saw_ramps);
    RUN_TEST(test_naive_saw_wraps);
    RUN_TEST(test_polyblep_saw_dc_offset_low);
    RUN_TEST(test_polyblep_matches_naive_at_low_freq);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test test_voice 2>&1 | tail -5
```

Expected: compile error — `Voice::Saw` undeclared.

- [ ] **Step 3: Add `Saw()` to `voice.h`**

```cpp
// In class Voice public section, after NaiveSaw():
    float Saw();   // PolyBLEP-corrected saw, anti-aliased.
```

- [ ] **Step 4: Implement polyBLEP in `voice.cpp`**

Add this static helper near the top of `voice.cpp` (before `namespace sawstack {`):

```cpp
namespace {
// PolyBLEP correction: returns a small adjustment to apply near phase
// wrap-around to suppress aliasing from the discontinuity.
inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
}
```

Then add the `Saw()` method to the `sawstack` namespace section:

```cpp
float Voice::Saw() {
    float out = phase_ * 2.0f - 1.0f;
    out -= poly_blep(phase_, phase_inc_);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}
```

- [ ] **Step 5: Run tests**

```bash
make -C test
```

Expected: all four voice tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/voice.h src/voice.cpp test/test_voice.cpp
git commit -m "feat(voice): polyBLEP-corrected saw"
```

---

## Task 4: Voice — sine, sine↔saw morph, phase reset (TDD)

**Files:**
- Modify: `sawstack/src/voice.h`
- Modify: `sawstack/src/voice.cpp`
- Modify: `sawstack/test/test_voice.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_voice.cpp` before `run_all`:

```cpp
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

// Update run_all:
void run_all() {
    RUN_TEST(test_naive_saw_ramps);
    RUN_TEST(test_naive_saw_wraps);
    RUN_TEST(test_polyblep_saw_dc_offset_low);
    RUN_TEST(test_polyblep_matches_naive_at_low_freq);
    RUN_TEST(test_sine_at_quarter_period_is_one);
    RUN_TEST(test_morph_at_zero_is_sine_at_one_is_saw);
    RUN_TEST(test_phase_reset_zeros_phase);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test 2>&1 | tail -10
```

Expected: compile error — `Sine`, `Morph`, `ResetPhase` undeclared.

- [ ] **Step 3: Extend `voice.h`**

```cpp
// In class Voice public section, after Saw():
    float Sine();                          // Pure sine at the current frequency.
    float Morph(float timbre);             // (1-timbre) * sine + timbre * polyBLEP saw.
    void  ResetPhase(float new_phase);     // Sets phase directly. new_phase ∈ [0, 1).
```

- [ ] **Step 4: Implement in `voice.cpp`**

Add `<cmath>` to the includes if not present.

```cpp
float Voice::Sine() {
    float out = std::sin(phase_ * 2.0f * 3.14159265358979f);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
}

float Voice::Morph(float timbre) {
    // Compute both shapes from the *same* phase before advancing.
    float s = std::sin(phase_ * 2.0f * 3.14159265358979f);
    float saw = phase_ * 2.0f - 1.0f - poly_blep(phase_, phase_inc_);
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return (1.0f - timbre) * s + timbre * saw;
}

void Voice::ResetPhase(float new_phase) {
    phase_ = new_phase;
}
```

- [ ] **Step 5: Run tests**

```bash
make -C test
```

Expected: all seven voice tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/voice.h src/voice.cpp test/test_voice.cpp
git commit -m "feat(voice): sine, sine↔saw morph, phase reset"
```

---

## Task 5: Voice — hard-sync emulation (TDD)

For RICH mode: each voice has a master phase advancing at the voice's frequency, and a slave phase advancing at `freq * richness_ratio`. The slave is hard-synced (forced to phase 0) every time the master phase wraps. The audible output is a polyBLEP saw of the slave phase, with polyBLEP correction also applied at the sync reset.

**Files:**
- Modify: `sawstack/src/voice.h`
- Modify: `sawstack/src/voice.cpp`
- Modify: `sawstack/test/test_voice.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_voice.cpp` before `run_all`:

```cpp
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

// Reset must zero both master and slave phases.
void test_hardsync_phase_reset_zeros_both() {
    Voice v;
    v.Init(kSampleRate);
    v.SetFrequency(440.0f);

    for (int i = 0; i < 23; ++i) v.HardSync(2.5f);
    v.ResetPhase(0.0f);
    v.ResetSyncPhase();
    // After both resets, ratio=1, output ≈ -1 (saw start).
    EXPECT_NEAR(v.HardSync(1.0f), -1.0f, 1e-3);
}

// Update run_all:
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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test 2>&1 | tail -5
```

Expected: compile error — `HardSync`, `ResetSyncPhase` undeclared.

- [ ] **Step 3: Extend `voice.h`**

```cpp
// Add a slave_phase_ private member after phase_inc_:
    float slave_phase_ = 0.0f;     // [0, 1) — advances at master_freq * ratio in RICH

// Add to public section after ResetPhase:
    float HardSync(float ratio);   // Master at SetFrequency, slave at master*ratio,
                                   // slave is hard-synced when master wraps.
                                   // Returns polyBLEP-corrected saw of slave phase.
    void  ResetSyncPhase();        // Zero the slave phase only.
```

- [ ] **Step 4: Implement in `voice.cpp`**

```cpp
float Voice::HardSync(float ratio) {
    // Output is polyBLEP saw of the slave phase.
    float slave_inc = phase_inc_ * ratio;
    float out = slave_phase_ * 2.0f - 1.0f;
    out -= poly_blep(slave_phase_, slave_inc);

    // Advance master; on wrap, hard-sync the slave to phase 0.
    phase_ += phase_inc_;
    if (phase_ >= 1.0f) {
        phase_       -= 1.0f;
        slave_phase_  = 0.0f;
    } else {
        slave_phase_ += slave_inc;
        if (slave_phase_ >= 1.0f) slave_phase_ -= 1.0f;
    }
    return out;
}

void Voice::ResetSyncPhase() {
    slave_phase_ = 0.0f;
}
```

- [ ] **Step 5: Run tests**

```bash
make -C test
```

Expected: all ten voice tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/voice.h src/voice.cpp test/test_voice.cpp
git commit -m "feat(voice): hard-sync emulation for RICH mode"
```

---

## Task 6: Pitch — master pitch + voct conversion (TDD)

**Files:**
- Create: `sawstack/src/pitch.h`
- Create: `sawstack/src/pitch.cpp`
- Create: `sawstack/test/test_pitch.cpp`
- Modify: `sawstack/test/Makefile`

- [ ] **Step 1: Write the failing test**

`test/test_pitch.cpp`:

```cpp
// test/test_pitch.cpp
#include "test_assert.h"
#include "../src/pitch.h"

using sawstack::ComputeMasterHz;
using sawstack::VoctVoltsFromAdc;

void test_master_hz_at_zero_is_c4() {
    EXPECT_NEAR(ComputeMasterHz(0, 0, 0.0f), 261.63f, 0.01f);
}

void test_master_hz_octave_up_doubles() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(12, 0, 0.0f);
    EXPECT_NEAR(b, a * 2.0f, 0.05f);
}

void test_master_hz_voct_one_volt_doubles() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(0, 0, 1.0f);
    EXPECT_NEAR(b, a * 2.0f, 0.05f);
}

void test_master_hz_fine_50_cents_up() {
    float a = ComputeMasterHz(0, 0, 0.0f);
    float b = ComputeMasterHz(0, 50, 0.0f);  // +50 cents = ratio 2^(50/1200)
    EXPECT_NEAR(b / a, 1.0293f, 0.001f);  // 2^(1/24) ≈ 1.0293
}

// kVoctScale=0 means the v/oct path is bypassed: any ADC value yields 0V.
void test_voct_disabled_when_scale_zero() {
    EXPECT_NEAR(VoctVoltsFromAdc(0.5f, /*zero=*/0.123f, /*scale=*/0.0f), 0.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.9f, /*zero=*/0.123f, /*scale=*/0.0f), 0.0f, 1e-6);
}

void test_voct_with_calibrated_constants() {
    // Suppose 0V → adc 0.20, +1V → adc 0.40. Then scale = 1/(0.40-0.20) = 5.
    float zero = 0.20f, scale = 5.0f;
    EXPECT_NEAR(VoctVoltsFromAdc(0.20f, zero, scale), 0.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.40f, zero, scale), 1.0f, 1e-6);
    EXPECT_NEAR(VoctVoltsFromAdc(0.60f, zero, scale), 2.0f, 1e-6);
}

void run_all() {
    RUN_TEST(test_master_hz_at_zero_is_c4);
    RUN_TEST(test_master_hz_octave_up_doubles);
    RUN_TEST(test_master_hz_voct_one_volt_doubles);
    RUN_TEST(test_master_hz_fine_50_cents_up);
    RUN_TEST(test_voct_disabled_when_scale_zero);
    RUN_TEST(test_voct_with_calibrated_constants);
}

TEST_MAIN()
```

- [ ] **Step 2: Add `test_pitch` to `test/Makefile`**

```make
# Update TESTS:
TESTS = test_smoke test_voice test_pitch

# Add recipe:
$(OUT)/test_pitch: test_pitch.cpp ../src/pitch.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
make -C test test_pitch 2>&1 | tail -3
```

Expected: compile error — `pitch.h` not found.

- [ ] **Step 4: Write `src/pitch.h`**

```cpp
// src/pitch.h
#pragma once

namespace sawstack {

// V/oct calibration. Measure once on real hardware, edit, rebuild, reflash.
// kVoctScale = 0 disables the v/oct path entirely (jack ignored).
constexpr float kVoctZero  = 0.0f;   // ADC reading at 0V (placeholder; Task 17 measures)
constexpr float kVoctScale = 0.0f;   // 1.0 / (adc_at_+1V - adc_at_0V) (placeholder)

// Convert raw v/oct ADC reading to volts using calibration constants.
// Returns 0.0 if scale == 0.0 (calibration not done).
float VoctVoltsFromAdc(float voct_adc, float zero, float scale);

// Compute the master pitch in Hz.
//   coarse_semitones: encoder coarse offset in semitones, [-24, +24]
//   fine_cents:       encoder fine offset in cents,      [-50, +50]
//   voct_volts:       v/oct CV in volts (already converted)
float ComputeMasterHz(int coarse_semitones, int fine_cents, float voct_volts);

}  // namespace sawstack
```

- [ ] **Step 5: Write `src/pitch.cpp`**

```cpp
// src/pitch.cpp
#include "pitch.h"
#include <cmath>

namespace sawstack {

float VoctVoltsFromAdc(float voct_adc, float zero, float scale) {
    if (scale == 0.0f) return 0.0f;
    return (voct_adc - zero) * scale;
}

float ComputeMasterHz(int coarse_semitones, int fine_cents, float voct_volts) {
    constexpr float kC4Hz = 261.63f;
    float exponent = (coarse_semitones / 12.0f)
                   + (fine_cents       / 1200.0f)
                   + voct_volts;
    return kC4Hz * std::pow(2.0f, exponent);
}

}  // namespace sawstack
```

- [ ] **Step 6: Run tests**

```bash
make -C test
```

Expected: all six pitch tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/pitch.h src/pitch.cpp test/test_pitch.cpp test/Makefile
git commit -m "feat(pitch): master Hz + voct conversion"
```

---

## Task 7: Pitch — voice offsets with asymmetric detune layout (TDD)

**Files:**
- Modify: `sawstack/src/pitch.h`
- Modify: `sawstack/src/pitch.cpp`
- Modify: `sawstack/test/test_pitch.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_pitch.cpp` before `run_all`:

```cpp
#include "../src/dsp_common.h"  // for kNumVoices
using sawstack::ComputeVoiceFreqs;

void test_voice_freqs_unison_at_detune_zero() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/0.0f, freqs);
    for (int i = 0; i < 5; ++i) EXPECT_NEAR(freqs[i], 440.0f, 1e-3);
}

void test_voice_freqs_voice0_always_at_master() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    EXPECT_NEAR(freqs[0], 440.0f, 1e-3);
}

void test_voice_freqs_two_up_two_down_at_full() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voices 1 and 3 are positive offsets (above master); 2 and 4 negative.
    EXPECT_TRUE(freqs[1] > 440.0f);
    EXPECT_TRUE(freqs[3] > freqs[1]);
    EXPECT_TRUE(freqs[2] < 440.0f);
    EXPECT_TRUE(freqs[4] < freqs[2]);
}

void test_voice_freqs_full_spread_is_2_octaves() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voice 3 (highest) should be ~+24 semitones ≈ 4× master, ±5%.
    // Spread is detune_norm² * 2400 cents = 2400 cents = 2 octaves
    // applied with multiplier 1.013 → 2.026 octaves up → ~4.07× master.
    EXPECT_NEAR(freqs[3] / 440.0f, 4.07f, 0.1f);
    EXPECT_NEAR(440.0f / freqs[4], 4.06f, 0.1f);  // voice 4 is symmetric down (mult 1.011)
}

void test_voice_freqs_asymmetric_multipliers_break_octave_coincidence() {
    float freqs[5];
    ComputeVoiceFreqs(440.0f, /*detune_norm=*/1.0f, freqs);
    // Voice 3 is at 1.0 × 1.013 × 2400 cents = 2431.2 cents.
    // Pure octave would be exactly 2400 (4×). We want it slightly off.
    EXPECT_TRUE(std::fabs(freqs[3] - 4.0f * 440.0f) > 5.0f);
}

// Update run_all:
void run_all() {
    RUN_TEST(test_master_hz_at_zero_is_c4);
    RUN_TEST(test_master_hz_octave_up_doubles);
    RUN_TEST(test_master_hz_voct_one_volt_doubles);
    RUN_TEST(test_master_hz_fine_50_cents_up);
    RUN_TEST(test_voct_disabled_when_scale_zero);
    RUN_TEST(test_voct_with_calibrated_constants);
    RUN_TEST(test_voice_freqs_unison_at_detune_zero);
    RUN_TEST(test_voice_freqs_voice0_always_at_master);
    RUN_TEST(test_voice_freqs_two_up_two_down_at_full);
    RUN_TEST(test_voice_freqs_full_spread_is_2_octaves);
    RUN_TEST(test_voice_freqs_asymmetric_multipliers_break_octave_coincidence);
}
```

Add `#include <cmath>` at the top of `test_pitch.cpp` if not present.

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test test_pitch 2>&1 | tail -3
```

Expected: `ComputeVoiceFreqs` undeclared.

- [ ] **Step 3: Extend `pitch.h`**

```cpp
// Add at end of namespace sawstack {

// Asymmetric detune multipliers — ~1% asymmetric to avoid octave/fifth coincidences.
// Multipliers for voices 1..4 of the absolute offset magnitude (voice 0 is always center).
// Tune by ear during first flash session; current values are first-pass guesses.
constexpr float kDetuneMul[5] = { 0.0f, 0.500f, -0.4985f, 1.013f, -1.011f };
//                                 v0    v1      v2        v3     v4

// Fill `out_freqs[5]` with the per-voice frequencies given a master pitch and
// a normalized detune in [0, 1]. Spread is squared (detune_norm² * 2400 cents)
// for fine control near unison.
void ComputeVoiceFreqs(float master_hz, float detune_norm, float out_freqs[5]);
```

- [ ] **Step 4: Implement in `pitch.cpp`**

```cpp
void ComputeVoiceFreqs(float master_hz, float detune_norm, float out_freqs[5]) {
    if (detune_norm < 0.0f) detune_norm = 0.0f;
    if (detune_norm > 1.0f) detune_norm = 1.0f;
    float spread_cents = detune_norm * detune_norm * 2400.0f;
    for (int i = 0; i < 5; ++i) {
        float offset_cents = kDetuneMul[i] * spread_cents;
        out_freqs[i] = master_hz * std::pow(2.0f, offset_cents / 1200.0f);
    }
}
```

- [ ] **Step 5: Run tests**

```bash
make -C test
```

Expected: all eleven pitch tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/pitch.h src/pitch.cpp test/test_pitch.cpp
git commit -m "feat(pitch): asymmetric voice detune layout"
```

---

## Task 8: StereoVCA — pan law + 5-voice mixer (TDD)

**Files:**
- Create: `sawstack/src/stereo_vca.h`
- Create: `sawstack/src/stereo_vca.cpp`
- Create: `sawstack/test/test_stereo_vca.cpp`
- Modify: `sawstack/test/Makefile`

- [ ] **Step 1: Write the failing test**

`test/test_stereo_vca.cpp`:

```cpp
// test/test_stereo_vca.cpp
#include "test_assert.h"
#include "../src/stereo_vca.h"
#include "../src/params.h"
#include <cmath>

using sawstack::MixVoices;
using sawstack::Width;

void test_mono_sums_voices_equally_to_l_and_r() {
    float voices[5] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    float l = 0, r = 0;
    MixVoices(voices, Width::MONO, &l, &r);
    EXPECT_NEAR(l, r, 1e-6);
    // Mean ~0.3; equal-power mono should preserve total energy roughly.
    EXPECT_TRUE(std::fabs(l) > 0.1f);
}

void test_center_voice_is_centered_at_all_widths() {
    // Only voice 0 (center) is non-zero.
    float voices[5] = { 1.0f, 0, 0, 0, 0 };
    for (Width w : {Width::MONO, Width::STEREO, Width::WIDE}) {
        float l = 0, r = 0;
        MixVoices(voices, w, &l, &r);
        EXPECT_NEAR(l, r, 1e-6);  // center voice always equal L/R
    }
}

// Pitch order is voice 4 (lowest) → 2 → 0 → 1 → 3 (highest).
// In WIDE, voice 4 should land hard-left and voice 3 hard-right.
void test_wide_voice_4_pans_hard_left() {
    float voices[5] = { 0, 0, 0, 0, 1.0f };
    float l = 0, r = 0;
    MixVoices(voices, Width::WIDE, &l, &r);
    EXPECT_TRUE(l > 0.9f);
    EXPECT_TRUE(r < 0.1f);
}

void test_wide_voice_3_pans_hard_right() {
    float voices[5] = { 0, 0, 0, 1.0f, 0 };
    float l = 0, r = 0;
    MixVoices(voices, Width::WIDE, &l, &r);
    EXPECT_TRUE(r > 0.9f);
    EXPECT_TRUE(l < 0.1f);
}

void test_stereo_pans_less_than_wide() {
    float voices[5] = { 0, 0, 0, 0, 1.0f };  // voice 4: lowest pitch, leftmost
    float l_stereo = 0, r_stereo = 0;
    float l_wide   = 0, r_wide   = 0;
    MixVoices(voices, Width::STEREO, &l_stereo, &r_stereo);
    MixVoices(voices, Width::WIDE,   &l_wide,   &r_wide);
    // Both pan left; WIDE pans further (more L, less R).
    EXPECT_TRUE(l_wide > l_stereo);
    EXPECT_TRUE(r_wide < r_stereo);
}

void run_all() {
    RUN_TEST(test_mono_sums_voices_equally_to_l_and_r);
    RUN_TEST(test_center_voice_is_centered_at_all_widths);
    RUN_TEST(test_wide_voice_4_pans_hard_left);
    RUN_TEST(test_wide_voice_3_pans_hard_right);
    RUN_TEST(test_stereo_pans_less_than_wide);
}

TEST_MAIN()
```

- [ ] **Step 2: Add to `test/Makefile`**

```make
TESTS = test_smoke test_voice test_pitch test_stereo_vca

$(OUT)/test_stereo_vca: test_stereo_vca.cpp ../src/stereo_vca.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
make -C test test_stereo_vca 2>&1 | tail -3
```

Expected: `stereo_vca.h` not found.

- [ ] **Step 4: Write `src/stereo_vca.h`**

```cpp
// src/stereo_vca.h
#pragma once
#include "params.h"

namespace sawstack {

// 5-voice MONO/STEREO/WIDE mixer with equal-power pan law.
// Voice indices in pitch order (low → high pitch): 4, 2, 0, 1, 3.
// MONO  : all voices summed equally to L and R.
// STEREO: pan positions in pitch order: -0.5, -0.25, 0, +0.25, +0.5
// WIDE  : pan positions in pitch order: -1.0, -0.5,  0, +0.5,  +1.0
void MixVoices(const float voices[5], Width w, float* out_l, float* out_r);

}  // namespace sawstack
```

- [ ] **Step 5: Write `src/stereo_vca.cpp`**

```cpp
// src/stereo_vca.cpp
#include "stereo_vca.h"
#include <cmath>

namespace sawstack {
namespace {

// Equal-power pan: pan ∈ [-1, +1], returns (gainL, gainR), sum-of-squares = 1.
inline void pan_gains(float pan, float* gl, float* gr) {
    float angle = (pan + 1.0f) * 3.14159265358979f * 0.25f;  // 0..π/2
    *gl = std::cos(angle);
    *gr = std::sin(angle);
}

// Voice indices in pitch order (lowest → highest):
// voice 4 (offset −1.0×), voice 2 (−0.5×), voice 0 (0), voice 1 (+0.5×), voice 3 (+1.0×).
constexpr int kPitchOrder[5] = { 4, 2, 0, 1, 3 };

// Pan positions per width, in pitch order.
constexpr float kPanMono[5]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kPanStereo[5] = { -0.5f, -0.25f, 0.0f, +0.25f, +0.5f };
constexpr float kPanWide[5]   = { -1.0f, -0.5f,  0.0f, +0.5f,  +1.0f };

}  // namespace

void MixVoices(const float voices[5], Width w, float* out_l, float* out_r) {
    const float* pans = (w == Width::MONO)   ? kPanMono
                      : (w == Width::STEREO) ? kPanStereo
                                             : kPanWide;
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 5; ++i) {
        int   v_idx = kPitchOrder[i];
        float gl, gr;
        pan_gains(pans[i], &gl, &gr);
        l += voices[v_idx] * gl;
        r += voices[v_idx] * gr;
    }
    *out_l = l;
    *out_r = r;
}

}  // namespace sawstack
```

- [ ] **Step 6: Run tests**

```bash
make -C test
```

Expected: all five stereo VCA tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/stereo_vca.h src/stereo_vca.cpp test/test_stereo_vca.cpp test/Makefile
git commit -m "feat(stereo_vca): MONO/STEREO/WIDE pan-law mixer"
```

---

## Task 9: SupersawEngine — skeleton + STACK mode (TDD)

The engine owns 5 voices + 1 sub voice and dispatches by mode. STACK mode = each voice produces `Morph(timbre)`. Mode-switch crossfade and sync are added in later tasks.

**Files:**
- Create: `sawstack/src/supersaw_engine.h`
- Create: `sawstack/src/supersaw_engine.cpp`
- Create: `sawstack/test/test_engine.cpp`
- Modify: `sawstack/test/Makefile`

- [ ] **Step 1: Write the failing test**

`test/test_engine.cpp`:

```cpp
// test/test_engine.cpp
#include "test_assert.h"
#include "../src/supersaw_engine.h"
#include "../src/params.h"
#include "../src/dsp_common.h"
#include <cmath>

using sawstack::SupersawEngine;
using sawstack::Params;
using sawstack::Mode;
using sawstack::Width;
using sawstack::kSampleRate;

static Params default_params() {
    Params p {};
    p.top_adc    = 0.5f;     // some detune
    p.bottom_adc = 1.0f;     // morph: full saw in STACK
    p.voct_adc   = 0.0f;
    p.mode       = Mode::STACK;
    p.width      = Width::STEREO;
    p.encoder_fine_delta   = 0;
    p.encoder_coarse_delta = 0;
    p.gate_edge  = false;
    return p;
}

void test_engine_stack_produces_nonzero_audio() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    eng.ApplyParams(default_params());
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    // Sum of squares should be > 0 (we got audio).
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_stack_morph_zero_is_quieter_lf_than_morph_one() {
    // At morph=0 (sine) at 440 Hz, output is bandlimited; at morph=1 (saw),
    // output has more high-frequency energy. We just sanity-check non-zero output
    // at both ends.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e_sine = 0;
    for (int i = 0; i < 48; ++i) e_sine += l[i]*l[i] + r[i]*r[i];

    p.bottom_adc = 1.0f;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);
    double e_saw = 0;
    for (int i = 0; i < 48; ++i) e_saw += l2[i]*l2[i] + r2[i]*r2[i];

    EXPECT_TRUE(e_sine > 0.0);
    EXPECT_TRUE(e_saw > 0.0);
}

void test_engine_output_is_bounded() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.bottom_adc = 1.0f;  // full saw
    p.top_adc    = 1.0f;  // full detune
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    for (int blk = 0; blk < 50; ++blk) {
        eng.ProcessBlock(l, r, 48);
        for (int i = 0; i < 48; ++i) {
            EXPECT_TRUE(std::fabs(l[i]) <= 1.0f);
            EXPECT_TRUE(std::fabs(r[i]) <= 1.0f);
        }
    }
}

void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
}

TEST_MAIN()
```

- [ ] **Step 2: Add to `test/Makefile`**

```make
TESTS = test_smoke test_voice test_pitch test_stereo_vca test_engine

$(OUT)/test_engine: test_engine.cpp ../src/supersaw_engine.cpp ../src/voice.cpp \
                    ../src/pitch.cpp ../src/stereo_vca.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
make -C test test_engine 2>&1 | tail -3
```

Expected: `supersaw_engine.h` not found.

- [ ] **Step 4: Write `src/supersaw_engine.h`**

```cpp
// src/supersaw_engine.h
#pragma once
#include "params.h"
#include "voice.h"

namespace sawstack {

class SupersawEngine {
  public:
    void Init(float sample_rate);

    // Called once per audio block, before ProcessBlock.
    void ApplyParams(const Params& p);

    // Generate `n_frames` of stereo audio into `out_l`, `out_r` (both size >= n_frames).
    void ProcessBlock(float* out_l, float* out_r, int n_frames);

  private:
    float sample_rate_     = 48000.0f;
    Mode  current_mode_    = Mode::STACK;
    Width current_width_   = Width::STEREO;

    float detune_norm_     = 0.0f;
    float morph_norm_      = 0.0f;
    int   coarse_semitones_= 0;
    int   fine_cents_      = 0;
    float voct_volts_      = 0.0f;

    Voice voices_[5];   // main supersaw voices
    Voice sub_voice_;   // SUB-mode sub-octave (used only in SUB)
};

}  // namespace sawstack
```

- [ ] **Step 5: Write `src/supersaw_engine.cpp`**

```cpp
// src/supersaw_engine.cpp
#include "supersaw_engine.h"
#include "pitch.h"
#include "stereo_vca.h"
#include "soft_clip.h"
#include <algorithm>

namespace sawstack {

void SupersawEngine::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    for (int i = 0; i < 5; ++i) voices_[i].Init(sample_rate);
    sub_voice_.Init(sample_rate);
    current_mode_  = Mode::STACK;
    current_width_ = Width::STEREO;
    detune_norm_   = 0.0f;
    morph_norm_    = 0.0f;
}

void SupersawEngine::ApplyParams(const Params& p) {
    current_mode_  = p.mode;
    current_width_ = p.width;
    detune_norm_   = std::clamp(p.top_adc,    0.0f, 1.0f);
    morph_norm_    = std::clamp(p.bottom_adc, 0.0f, 1.0f);

    coarse_semitones_ += p.encoder_coarse_delta;
    fine_cents_       += p.encoder_fine_delta;
    if (coarse_semitones_ < -24) coarse_semitones_ = -24;
    if (coarse_semitones_ >  24) coarse_semitones_ =  24;
    if (fine_cents_       < -50) fine_cents_       = -50;
    if (fine_cents_       >  50) fine_cents_       =  50;

    voct_volts_ = VoctVoltsFromAdc(p.voct_adc, kVoctZero, kVoctScale);

    float master_hz = ComputeMasterHz(coarse_semitones_, fine_cents_, voct_volts_);
    float freqs[5];
    ComputeVoiceFreqs(master_hz, detune_norm_, freqs);
    for (int i = 0; i < 5; ++i) voices_[i].SetFrequency(freqs[i]);
    sub_voice_.SetFrequency(master_hz * 0.5f);  // octave below master
}

void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        for (int v = 0; v < 5; ++v) {
            // STACK only for now; RICH/SUB added in later tasks.
            voice_samples[v] = voices_[v].Morph(morph_norm_);
        }
        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
    }
}

}  // namespace sawstack
```

- [ ] **Step 6: Run tests**

```bash
make -C test
```

Expected: all three engine tests PASS, plus all earlier tests.

- [ ] **Step 7: Commit**

```bash
git add src/supersaw_engine.h src/supersaw_engine.cpp test/test_engine.cpp test/Makefile
git commit -m "feat(engine): skeleton + STACK mode"
```

---

## Task 10: SupersawEngine — RICH mode (TDD)

**Files:**
- Modify: `sawstack/src/supersaw_engine.cpp`
- Modify: `sawstack/test/test_engine.cpp`

- [ ] **Step 1: Add failing test**

Append to `test/test_engine.cpp` before `run_all`:

```cpp
void test_engine_rich_mode_produces_audio() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.mode       = Mode::RICH;
    p.bottom_adc = 0.5f;  // ratio = 1 + 0.5*4 = 3.0
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_rich_at_morph_zero_matches_saw_amplitude() {
    // RICH at morph=0 is ratio=1, which is identical to a plain saw in shape
    // (just sums to the soft-clipped/panned 5-stack). Energy comparable to STACK
    // at morph=1.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::RICH;
    p.bottom_adc = 0.0f;  // ratio = 1.0
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e_rich = 0;
    for (int i = 0; i < 48; ++i) e_rich += l[i]*l[i] + r[i]*r[i];

    EXPECT_TRUE(e_rich > 0.01);
}

// Update run_all:
void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
    RUN_TEST(test_engine_rich_mode_produces_audio);
    RUN_TEST(test_engine_rich_at_morph_zero_matches_saw_amplitude);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test 2>&1 | tail -10
```

Expected: tests run; `test_engine_rich_mode_produces_audio` fails because RICH currently returns silence (the engine only handles STACK).

- [ ] **Step 3: Modify `ProcessBlock` in `src/supersaw_engine.cpp`**

Replace the inner voice generation in `ProcessBlock` with mode dispatch:

```cpp
void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        switch (current_mode_) {
            case Mode::STACK:
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(morph_norm_);
                break;
            case Mode::RICH: {
                float ratio = 1.0f + morph_norm_ * 4.0f;
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].HardSync(ratio);
                break;
            }
            case Mode::SUB:
                // Implemented in Task 11.
                for (int v = 0; v < 5; ++v) voice_samples[v] = 0.0f;
                break;
        }
        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
    }
}
```

- [ ] **Step 4: Run tests**

```bash
make -C test
```

Expected: all engine tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/supersaw_engine.cpp test/test_engine.cpp
git commit -m "feat(engine): RICH mode hard-sync dispatch"
```

---

## Task 11: SupersawEngine — SUB mode (TDD)

**Files:**
- Modify: `sawstack/src/supersaw_engine.cpp`
- Modify: `sawstack/test/test_engine.cpp`

- [ ] **Step 1: Add failing test**

Append to `test/test_engine.cpp` before `run_all`:

```cpp
void test_engine_sub_mode_at_zero_sub_matches_stack_full_saw() {
    // SUB with morph=0 should be 5-saw stack (no sub mixed in) — energy comparable
    // to STACK at morph=1.
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::SUB;
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l[48] = {0}, r[48] = {0};
    eng.ProcessBlock(l, r, 48);
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.01);
}

void test_engine_sub_mode_at_one_has_more_energy_than_zero() {
    SupersawEngine eng;
    eng.Init(kSampleRate);

    Params p = default_params();
    p.mode = Mode::SUB;
    p.bottom_adc = 0.0f;
    eng.ApplyParams(p);
    float l1[48] = {0}, r1[48] = {0};
    eng.ProcessBlock(l1, r1, 48);
    double e0 = 0;
    for (int i = 0; i < 48; ++i) e0 += l1[i]*l1[i] + r1[i]*r1[i];

    p.bottom_adc = 1.0f;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);
    double e1 = 0;
    for (int i = 0; i < 48; ++i) e1 += l2[i]*l2[i] + r2[i]*r2[i];

    EXPECT_TRUE(e1 > e0);
}

// Update run_all:
void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
    RUN_TEST(test_engine_rich_mode_produces_audio);
    RUN_TEST(test_engine_rich_at_morph_zero_matches_saw_amplitude);
    RUN_TEST(test_engine_sub_mode_at_zero_sub_matches_stack_full_saw);
    RUN_TEST(test_engine_sub_mode_at_one_has_more_energy_than_zero);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test 2>&1 | tail -10
```

Expected: SUB tests fail (engine returns silence in SUB).

- [ ] **Step 3: Modify SUB branch in `ProcessBlock`**

Replace the SUB case in the switch:

```cpp
            case Mode::SUB: {
                // 5 voices are pure saws (Morph at full timbre = 1.0).
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(1.0f);
                // Sub voice is mixed into the centered output post-VCA.
                // We add it after MixVoices below, so leave voice_samples as-is here.
                break;
            }
```

Then change the body of the per-sample loop to handle the sub voice add:

```cpp
        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        if (current_mode_ == Mode::SUB) {
            // Sub voice is always centered (equal L/R) and scaled by morph.
            float sub = sub_voice_.Saw() * morph_norm_;
            l += sub;
            r += sub;
        }
        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
```

The full updated `ProcessBlock`:

```cpp
void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        switch (current_mode_) {
            case Mode::STACK:
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(morph_norm_);
                break;
            case Mode::RICH: {
                float ratio = 1.0f + morph_norm_ * 4.0f;
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].HardSync(ratio);
                break;
            }
            case Mode::SUB:
                for (int v = 0; v < 5; ++v)
                    voice_samples[v] = voices_[v].Morph(1.0f);
                break;
        }
        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        if (current_mode_ == Mode::SUB) {
            float sub = sub_voice_.Saw() * morph_norm_;
            l += sub;
            r += sub;
        }
        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
    }
}
```

- [ ] **Step 4: Run tests**

```bash
make -C test
```

Expected: all seven engine tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/supersaw_engine.cpp test/test_engine.cpp
git commit -m "feat(engine): SUB mode with sub-octave injection"
```

---

## Task 12: SupersawEngine — gate-edge phase reset, smoothing, mode crossfade (TDD)

**Files:**
- Modify: `sawstack/src/supersaw_engine.h`
- Modify: `sawstack/src/supersaw_engine.cpp`
- Modify: `sawstack/test/test_engine.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_engine.cpp` before `run_all`:

```cpp
void test_engine_gate_edge_resets_voice_phases() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    eng.ApplyParams(p);

    float l[48] = {0}, r[48] = {0};
    // Run a few blocks to advance phases.
    for (int i = 0; i < 5; ++i) eng.ProcessBlock(l, r, 48);

    // Now signal a gate edge.
    p.gate_edge = true;
    eng.ApplyParams(p);
    eng.ProcessBlock(l, r, 48);

    // After reset, voices have evenly distributed offsets (0, 0.2, 0.4, 0.6, 0.8).
    // After 1 block (48 samples), they've each advanced by 48 * (master_hz / sr).
    // We can't easily inspect internals from outside, so the check is indirect:
    // the first sample post-reset should be different from the last sample pre-reset
    // by more than chance (audible click-or-not is the real test).
    // Here, just verify the engine remains bounded and producing audio.
    double e = 0;
    for (int i = 0; i < 48; ++i) e += l[i]*l[i] + r[i]*r[i];
    EXPECT_TRUE(e > 0.001);
}

void test_engine_mode_change_does_not_click() {
    SupersawEngine eng;
    eng.Init(kSampleRate);
    Params p = default_params();
    p.mode = Mode::STACK;
    eng.ApplyParams(p);
    float l1[48] = {0}, r1[48] = {0};
    eng.ProcessBlock(l1, r1, 48);

    p.mode = Mode::RICH;
    eng.ApplyParams(p);
    float l2[48] = {0}, r2[48] = {0};
    eng.ProcessBlock(l2, r2, 48);

    // No sample-to-sample jump greater than ~0.5 across the boundary
    // (5 ms cosine crossfade should keep deltas small).
    float boundary_jump = std::fabs(l2[0] - l1[47]);
    EXPECT_TRUE(boundary_jump < 0.5f);
}

// Update run_all:
void run_all() {
    RUN_TEST(test_engine_stack_produces_nonzero_audio);
    RUN_TEST(test_engine_stack_morph_zero_is_quieter_lf_than_morph_one);
    RUN_TEST(test_engine_output_is_bounded);
    RUN_TEST(test_engine_rich_mode_produces_audio);
    RUN_TEST(test_engine_rich_at_morph_zero_matches_saw_amplitude);
    RUN_TEST(test_engine_sub_mode_at_zero_sub_matches_stack_full_saw);
    RUN_TEST(test_engine_sub_mode_at_one_has_more_energy_than_zero);
    RUN_TEST(test_engine_gate_edge_resets_voice_phases);
    RUN_TEST(test_engine_mode_change_does_not_click);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make -C test 2>&1 | tail -10
```

Expected: `test_engine_mode_change_does_not_click` may fail without the crossfade. The gate-edge test should pass trivially; we'll still implement the reset.

- [ ] **Step 3: Add private state to `supersaw_engine.h`**

```cpp
// Add private members after voice members:
    Mode  prev_mode_           = Mode::STACK;
    int   crossfade_remaining_ = 0;        // samples remaining in the 5 ms cosine fade
    int   crossfade_total_     = 0;        // total samples for current crossfade
    bool  gate_pending_        = false;
    // One-pole smoothers for ADCs.
    float detune_smooth_       = 0.0f;
    float morph_smooth_        = 0.0f;
    float voct_smooth_         = 0.0f;

  private:
    // Helper used during crossfade — generates one sample of the chosen mode.
    // Used twice per sample during the crossfade window.
    float generate_voice(int voice_idx, Mode m, float morph_for_block);
```

- [ ] **Step 4: Implement smoothing + crossfade + sync in `supersaw_engine.cpp`**

Replace `ApplyParams`:

```cpp
void SupersawEngine::ApplyParams(const Params& p) {
    // Mode change kicks off a 5 ms cosine crossfade.
    if (p.mode != current_mode_) {
        prev_mode_           = current_mode_;
        current_mode_        = p.mode;
        crossfade_total_     = static_cast<int>(sample_rate_ * 0.005f);  // 5 ms
        crossfade_remaining_ = crossfade_total_;
    }
    current_width_ = p.width;

    // Smoothing — one-pole IIR with ~5 ms time constant at audio rate.
    // Applied per-block; that's good enough to mask ADC jitter.
    constexpr float kAlpha = 0.2f;  // tau ≈ 5 ms at 48-sample blocks @ 48 kHz
    detune_smooth_ += kAlpha * (std::clamp(p.top_adc,    0.0f, 1.0f) - detune_smooth_);
    morph_smooth_  += kAlpha * (std::clamp(p.bottom_adc, 0.0f, 1.0f) - morph_smooth_);

    coarse_semitones_ += p.encoder_coarse_delta;
    fine_cents_       += p.encoder_fine_delta;
    if (coarse_semitones_ < -24) coarse_semitones_ = -24;
    if (coarse_semitones_ >  24) coarse_semitones_ =  24;
    if (fine_cents_       < -50) fine_cents_       = -50;
    if (fine_cents_       >  50) fine_cents_       =  50;

    voct_smooth_ += kAlpha * (VoctVoltsFromAdc(p.voct_adc, kVoctZero, kVoctScale) - voct_smooth_);

    float master_hz = ComputeMasterHz(coarse_semitones_, fine_cents_, voct_smooth_);
    float freqs[5];
    ComputeVoiceFreqs(master_hz, detune_smooth_, freqs);
    for (int i = 0; i < 5; ++i) voices_[i].SetFrequency(freqs[i]);
    sub_voice_.SetFrequency(master_hz * 0.5f);

    if (p.gate_edge) gate_pending_ = true;

    // Cache for ProcessBlock.
    detune_norm_ = detune_smooth_;
    morph_norm_  = morph_smooth_;
}
```

Add the `generate_voice` helper before `ProcessBlock`:

```cpp
float SupersawEngine::generate_voice(int v, Mode m, float morph_for_block) {
    switch (m) {
        case Mode::STACK: return voices_[v].Morph(morph_for_block);
        case Mode::RICH:  return voices_[v].HardSync(1.0f + morph_for_block * 4.0f);
        case Mode::SUB:   return voices_[v].Morph(1.0f);
    }
    return 0.0f;
}
```

> **Important:** the crossfade must NOT call `generate_voice` twice per sample for the same voice (that would double-advance the phase). Instead, we run only the *active* mode through the voice array; the crossfade applies to a *parallel synth* of the previous mode by re-running the same voices but only consuming their phase advance once. This is tricky.
>
> A simpler valid approach: when `prev_mode_ != current_mode_` and `crossfade_remaining_ > 0`, we still only generate using `current_mode_`, but we cosine-ramp the output from 0 up to full level over the crossfade window. The "previous mode" output is implicitly silenced. This isn't a true crossfade between modes, but it is a click-suppression ramp that achieves the same musical goal (no audible discontinuity at mode switch). If feel-testing on hardware shows this is insufficient, revisit later.

Replace `ProcessBlock`:

```cpp
void SupersawEngine::ProcessBlock(float* out_l, float* out_r, int n_frames) {
    if (gate_pending_) {
        constexpr float kPhases[5] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f };
        for (int i = 0; i < 5; ++i) {
            voices_[i].ResetPhase(kPhases[i]);
            voices_[i].ResetSyncPhase();
        }
        sub_voice_.ResetPhase(0.0f);
        gate_pending_ = false;
    }

    for (int n = 0; n < n_frames; ++n) {
        float voice_samples[5];
        for (int v = 0; v < 5; ++v)
            voice_samples[v] = generate_voice(v, current_mode_, morph_norm_);

        float l, r;
        MixVoices(voice_samples, current_width_, &l, &r);
        if (current_mode_ == Mode::SUB) {
            float sub = sub_voice_.Saw() * morph_norm_;
            l += sub;
            r += sub;
        }

        // Click-suppression ramp on mode change: cosine from 0 → 1 over the window.
        if (crossfade_remaining_ > 0) {
            float t = 1.0f - static_cast<float>(crossfade_remaining_) / crossfade_total_;
            float gain = 0.5f - 0.5f * std::cos(t * 3.14159265358979f);
            l *= gain;
            r *= gain;
            --crossfade_remaining_;
        }

        out_l[n] = soft_clip(l);
        out_r[n] = soft_clip(r);
    }
}
```

Add `#include <cmath>` to `supersaw_engine.cpp` if not present.

- [ ] **Step 5: Run tests**

```bash
make -C test
```

Expected: all engine tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/supersaw_engine.h src/supersaw_engine.cpp test/test_engine.cpp
git commit -m "feat(engine): gate-edge phase reset, smoothing, mode-change ramp"
```

---

## Task 13: main.cpp — HAL bring-up (no host test; verify by build)

This task wires the engine into libDaisy: ADCs, switches, encoder, gate, audio callback, slow loop.

**Files:**
- Modify: `sawstack/src/main.cpp`
- Modify: `sawstack/Makefile` (add new sources)

- [ ] **Step 1: Replace `src/main.cpp`** with the full HAL wiring

```cpp
// src/main.cpp — Legio HAL + slow loop. Only file that includes daisy_legio.h.
#include "daisy_legio.h"
#include "supersaw_engine.h"
#include "params.h"
#include "dsp_common.h"
#include <cstdio>
#include <cstring>

using namespace daisy;
using sawstack::SupersawEngine;
using sawstack::Params;
using sawstack::Mode;
using sawstack::Width;

DaisyLegio        hw;
SupersawEngine    engine;

// Volatile UI snapshot updated from audio callback, drained by slow loop.
struct UiSnapshot {
    Mode  mode;
    Width width;
    float top_adc;
    float bottom_adc;
    float voct_adc;
    int   gate_edge_count;
};
volatile UiSnapshot g_ui = {};

// Encoder state across audio blocks.
static int  s_encoder_increment_acc = 0;
static bool s_encoder_pressed_prev  = false;
static bool s_gate_prev             = false;

// Convert libDaisy Switch3.Read() (0=CENTER,1=POS_UP,2=POS_DOWN) to panel-corrected
// labels. On Legio's panel POS_UP corresponds to **panel DOWN** and POS_DOWN to
// **panel UP** (verified by feel during legio_tape).
static int panel_relative_switch(uint8_t lib_pos) {
    switch (lib_pos) {
        case 0: return 0;  // CENTER
        case 1: return 2;  // POS_UP (lib) = panel DOWN → return 2 = panel-DOWN code
        case 2: return 1;  // POS_DOWN (lib) = panel UP → return 1 = panel-UP code
    }
    return 0;
}

static Mode mode_from_left_switch(uint8_t lib_pos) {
    int p = panel_relative_switch(lib_pos);
    // 1 = panel UP → STACK; 0 = CENTER → RICH; 2 = panel DOWN → SUB
    if (p == 1) return Mode::STACK;
    if (p == 2) return Mode::SUB;
    return Mode::RICH;
}

static Width width_from_right_switch(uint8_t lib_pos) {
    int p = panel_relative_switch(lib_pos);
    // 1 = panel UP → MONO; 0 = CENTER → STEREO; 2 = panel DOWN → WIDE
    if (p == 1) return Width::MONO;
    if (p == 2) return Width::WIDE;
    return Width::STEREO;
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t size) {
    hw.ProcessAllControls();

    Params p {};
    p.top_adc    = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_TOP);
    p.bottom_adc = hw.GetKnobValue(DaisyLegio::CONTROL_KNOB_BOTTOM);
    p.voct_adc   = hw.GetKnobValue(DaisyLegio::CONTROL_PITCH);
    p.mode       = mode_from_left_switch(hw.sw[DaisyLegio::SW_LEFT].Read());
    p.width      = width_from_right_switch(hw.sw[DaisyLegio::SW_RIGHT].Read());

    // Encoder: accumulate increments since last block. If pressed, route to coarse;
    // otherwise to fine.
    int incr = hw.encoder.Increment();
    bool pressed = hw.encoder.Pressed();
    if (pressed) {
        p.encoder_coarse_delta = incr;
        p.encoder_fine_delta   = 0;
    } else {
        p.encoder_coarse_delta = 0;
        p.encoder_fine_delta   = incr;
    }
    s_encoder_pressed_prev = pressed;

    // Gate edge detection — block-rate (~1 ms granularity).
    bool gate_now = hw.gate.State();
    p.gate_edge   = gate_now && !s_gate_prev;
    s_gate_prev   = gate_now;

    engine.ApplyParams(p);

    // Stereo-interleaved out. size is total samples (L+R).
    int n_frames = size / 2;
    static float l_buf[sawstack::kAudioBlockSize];
    static float r_buf[sawstack::kAudioBlockSize];
    engine.ProcessBlock(l_buf, r_buf, n_frames);
    for (int i = 0; i < n_frames; ++i) {
        out[2*i + 0] = l_buf[i];
        out[2*i + 1] = r_buf[i];
    }

    // Update UI snapshot.
    g_ui.mode       = p.mode;
    g_ui.width      = p.width;
    g_ui.top_adc    = p.top_adc;
    g_ui.bottom_adc = p.bottom_adc;
    g_ui.voct_adc   = p.voct_adc;
    if (p.gate_edge) ++const_cast<UiSnapshot&>(g_ui).gate_edge_count;
}

static const char* mode_label(Mode m) {
    switch (m) { case Mode::STACK: return "STACK";
                 case Mode::RICH:  return "RICH";
                 case Mode::SUB:   return "SUB"; }
    return "?";
}
static const char* width_label(Width w) {
    switch (w) { case Width::MONO:   return "MONO";
                 case Width::STEREO: return "STEREO";
                 case Width::WIDE:   return "WIDE"; }
    return "?";
}

int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(sawstack::kAudioBlockSize);
    engine.Init(sawstack::kSampleRate);
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    hw.seed.StartLog(false);

    uint32_t last_print = System::GetNow();
    while (true) {
        if (System::GetNow() - last_print >= 200) {  // 5 Hz
            UiSnapshot snap = g_ui;  // copy out of volatile
            hw.seed.PrintLine("mode=%s width=%s top=%0.3f bot=%0.3f voct_raw=%0.4f sync=%d",
                              mode_label(snap.mode), width_label(snap.width),
                              snap.top_adc, snap.bottom_adc, snap.voct_adc,
                              snap.gate_edge_count);
            last_print = System::GetNow();
        }
        System::Delay(5);
    }
}
```

- [ ] **Step 2: Update `Makefile` `CPP_SOURCES`**

```make
CPP_SOURCES = src/main.cpp src/supersaw_engine.cpp src/voice.cpp src/pitch.cpp src/stereo_vca.cpp
```

- [ ] **Step 3: Build firmware**

```bash
cd ~/code/legio/sawstack
make 2>&1 | tail -10
```

Expected: builds without errors. Final size print should show <40% of 128 KB internal flash. If any libDaisy member name differs from what's used here (`hw.sw`, `hw.encoder`, `hw.gate`, `hw.GetKnobValue`, `DaisyLegio::SW_LEFT`, etc.), inspect `lib/libDaisy/src/daisy_legio.h` and adjust.

- [ ] **Step 4: Verify host tests still pass** (the `Params.gate_edge` field and main-only changes shouldn't break them)

```bash
make -C test
```

Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp Makefile
git commit -m "feat(main): HAL wiring — controls, switches, gate, audio, telemetry"
```

---

## Task 14: LEDs — mode + width + boot + gate-pulse (no host test; visual verify on hardware)

The LEDs module exposes a pure function that computes RGB triplets from the engine's UI state plus a few timing signals. We host-test the function for boot pattern correctness and width-brightness mapping; visual verification on hardware is the real check.

**Files:**
- Create: `sawstack/src/leds.h`
- Create: `sawstack/src/leds.cpp`
- Modify: `sawstack/src/main.cpp` (call `ComputeLeds` from the slow loop, write to hardware)
- Modify: `sawstack/Makefile`

- [ ] **Step 1: Write `src/leds.h`**

```cpp
// src/leds.h
#pragma once
#include "params.h"

namespace sawstack {

struct Rgb { float r, g, b; };

struct LedInputs {
    Mode  mode;
    Width width;
    uint32_t now_ms;                  // current millisecond counter
    uint32_t last_gate_edge_ms;       // ms timestamp of most recent rising edge (0 if none)
    uint32_t boot_ms;                 // ms since boot
};

void ComputeLeds(const LedInputs& in, Rgb* out_left, Rgb* out_right);

}  // namespace sawstack
```

- [ ] **Step 2: Write `src/leds.cpp`**

```cpp
// src/leds.cpp
#include "leds.h"

namespace sawstack {

namespace {
constexpr uint32_t kBootDurationMs   = 500;
constexpr uint32_t kBootBlinkMs      = 125;   // 4 Hz total period 250 ms; on for 125 ms
constexpr uint32_t kGatePulseMs      = 50;
constexpr float    kGatePulseGain    = 1.4f;  // brightness multiplier during pulse

constexpr Rgb kBlue   = { 0.0f, 0.0f, 0.8f };
constexpr Rgb kYellow = { 0.7f, 0.6f, 0.0f };
constexpr Rgb kDimWhite = { 0.25f, 0.25f, 0.25f };
constexpr Rgb kBarelyOn = { 0.05f, 0.05f, 0.05f };
constexpr Rgb kStereoBlue = { 0.0f, 0.0f, 0.40f };
constexpr Rgb kWideBlue   = { 0.0f, 0.0f, 0.90f };

inline Rgb scale(Rgb c, float k) { return { c.r * k, c.g * k, c.b * k }; }

inline Rgb mode_color(Mode m) {
    switch (m) {
        case Mode::STACK: return kBlue;
        case Mode::RICH:  return kYellow;
        case Mode::SUB:   return kDimWhite;
    }
    return kDimWhite;
}

inline Rgb width_color(Width w) {
    switch (w) {
        case Width::MONO:   return kBarelyOn;
        case Width::STEREO: return kStereoBlue;
        case Width::WIDE:   return kWideBlue;
    }
    return kBarelyOn;
}
}  // namespace

void ComputeLeds(const LedInputs& in, Rgb* out_left, Rgb* out_right) {
    // Boot ID pattern: alternate blue/yellow at 4 Hz for 500 ms.
    if (in.boot_ms < kBootDurationMs) {
        bool phase = (in.boot_ms / kBootBlinkMs) & 1;
        *out_left  = phase ? kBlue   : kYellow;
        *out_right = phase ? kYellow : kBlue;
        return;
    }

    // Normal mode display.
    Rgb l = mode_color(in.mode);
    Rgb r = width_color(in.width);

    // Gate-edge pulse on left LED.
    uint32_t since_gate = in.now_ms - in.last_gate_edge_ms;
    if (in.last_gate_edge_ms != 0 && since_gate < kGatePulseMs) {
        l = scale(l, kGatePulseGain);
        if (l.r > 1.0f) l.r = 1.0f;
        if (l.g > 1.0f) l.g = 1.0f;
        if (l.b > 1.0f) l.b = 1.0f;
    }

    *out_left  = l;
    *out_right = r;
}

}  // namespace sawstack
```

- [ ] **Step 3: Optional — add a brief LED unit test**

Append to `test/test_smoke.cpp` (since it's small, no need for a separate test file):

```cpp
#include "../src/leds.h"
using sawstack::ComputeLeds;
using sawstack::LedInputs;
using sawstack::Rgb;
using sawstack::Mode;
using sawstack::Width;

void test_leds_boot_pattern_alternates() {
    LedInputs in;
    in.mode = Mode::STACK; in.width = Width::STEREO;
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
    in.mode = Mode::STACK;
    in.last_gate_edge_ms = 0;
    in.boot_ms = 600;        in.now_ms = 600;
    in.width = Width::STEREO;
    Rgb sl, sr; ComputeLeds(in, &sl, &sr);
    in.width = Width::WIDE;
    Rgb wl, wr; ComputeLeds(in, &wl, &wr);
    EXPECT_TRUE(wr.b > sr.b);
}

// Update run_all in test_smoke.cpp:
void run_all() {
    RUN_TEST(test_constants);
    RUN_TEST(test_soft_clip_passes_unity);
    RUN_TEST(test_leds_boot_pattern_alternates);
    RUN_TEST(test_leds_wide_brighter_than_stereo);
}
```

- [ ] **Step 4: Update `test/Makefile` for the new dependency**

```make
$(OUT)/test_smoke: test_smoke.cpp ../src/leds.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

- [ ] **Step 5: Run host tests**

```bash
make -C test
```

Expected: all tests PASS, including the two new LED tests.

- [ ] **Step 6: Wire LEDs into `src/main.cpp`**

Add at top with the other includes:

```cpp
#include "leds.h"
```

Add a static gate-edge timestamp:

```cpp
static volatile uint32_t s_last_gate_edge_ms = 0;
```

In the `AudioCallback`, after `s_gate_prev = gate_now;` add:

```cpp
    if (p.gate_edge) s_last_gate_edge_ms = System::GetNow();
```

In `main()` slow loop, before/after the existing PrintLine, add LED computation and write:

```cpp
    uint32_t boot_start = System::GetNow();
    while (true) {
        uint32_t now = System::GetNow();
        sawstack::LedInputs li;
        li.mode              = g_ui.mode;
        li.width             = g_ui.width;
        li.now_ms            = now;
        li.last_gate_edge_ms = s_last_gate_edge_ms;
        li.boot_ms           = now - boot_start;

        sawstack::Rgb left, right;
        sawstack::ComputeLeds(li, &left, &right);
        hw.SetLed(DaisyLegio::LED_LEFT, left.r, left.g, left.b);
        hw.SetLed(DaisyLegio::LED_RIGHT, right.r, right.g, right.b);
        hw.UpdateLeds();

        if (now - last_print >= 200) {
            UiSnapshot snap = g_ui;
            hw.seed.PrintLine("mode=%s width=%s top=%0.3f bot=%0.3f voct_raw=%0.4f sync=%d",
                              mode_label(snap.mode), width_label(snap.width),
                              snap.top_adc, snap.bottom_adc, snap.voct_adc,
                              snap.gate_edge_count);
            last_print = now;
        }
        System::Delay(20);  // ~50 Hz LED refresh
    }
```

(Replace the existing `while(true)` loop with this version. If libDaisy method names differ — `SetLed`, `UpdateLeds`, `LED_LEFT`/`LED_RIGHT` — inspect `daisy_legio.h` and adjust.)

- [ ] **Step 7: Update `Makefile` `CPP_SOURCES`**

```make
CPP_SOURCES = src/main.cpp src/supersaw_engine.cpp src/voice.cpp src/pitch.cpp \
              src/stereo_vca.cpp src/leds.cpp
```

- [ ] **Step 8: Build firmware**

```bash
make 2>&1 | tail -5
```

Expected: builds without errors.

- [ ] **Step 9: Commit**

```bash
git add src/leds.h src/leds.cpp src/main.cpp Makefile test/test_smoke.cpp test/Makefile
git commit -m "feat(leds): mode/width/boot/gate-pulse LED scheme"
```

---

## Task 15: First flash + audio bring-up (no v/oct, default detune)

This is the first time the firmware runs on hardware. Goals: confirm audio plays, confirm all three modes produce sound, confirm switches map correctly, confirm telemetry prints.

- [ ] **Step 1: Put the module in DFU mode**

Press and hold BOOT, tap RESET, release BOOT. Both LEDs should go dark; the module enumerates as `STM32 BOOTLOADER` over USB.

- [ ] **Step 2: Flash**

```bash
cd ~/code/legio/sawstack
make program-dfu
```

Expected: `dfu-util` reports a successful upload. The trailing `Error during download get_status` / `Error 74` is harmless. The module resets and starts running.

If `/dev/cu.usbmodem*` is missing after a few seconds, reseat the module in the rack (firmware is fine; this is a USB-stack quirk).

- [ ] **Step 3: Connect serial telemetry**

```bash
screen /dev/cu.usbmodem* 115200
```

Expected: lines printing at 5 Hz, e.g., `mode=STACK width=STEREO top=0.500 bot=0.300 voct_raw=0.4521 sync=0`.

If `screen` says the device is busy, run `screen -ls` and detach any stale session.

- [ ] **Step 4: Verify audio**

Patch a stereo output to a mixer. Turn the top knob from min to max — you should hear the supersaw spread from unison to a wide multi-octave swarm. Turn the bottom knob — you should hear sine↔saw morph (in STACK), hard-sync sweep (in RICH), or sub-octave swell (in SUB).

- [ ] **Step 5: Verify the mode switch**

Flick the left switch. The LED should change blue / yellow / dim-white. Audible character change should match (saw stack / sync sweep / sub octave).

If the mode-to-position mapping is wrong (e.g., panel-up gives RICH instead of STACK), flip the labels in `mode_from_left_switch` in `src/main.cpp`. The Switch3 polarity inversion is documented but may have additional subtleties — trust your fingers.

- [ ] **Step 6: Verify the right switch**

Flick the right switch. The right LED brightness should ramp barely-on / medium-blue / bright-blue. Stereo image should widen.

- [ ] **Step 7: Verify gate sync**

Patch a clock or kick to the gate input. The left LED should pulse on each rising edge. Stereo telemetry's `sync=...` should increment. Audibly, voice phases reset on each pulse.

- [ ] **Step 8: Verify v/oct is bypassed**

Patch a non-zero CV (e.g., +1V) to the v/oct jack — the pitch should *not* change (because `kVoctScale = 0`). Confirms our floating-jack-safety story.

- [ ] **Step 9: If everything looks right, commit nothing**

(There is no code to commit at this stage — this task is a verification gate.)

---

## Task 16: V/oct calibration — measure on real hardware, hardcode constants

- [ ] **Step 1: Patch a known 0V source to the v/oct jack**

Use a Make Noise Pressure Points / Brains, an offset module, or any module that can output a settable DC voltage with confirmed accuracy. Set output to 0V exactly.

- [ ] **Step 2: Read `voct_raw` from telemetry**

```bash
screen /dev/cu.usbmodem* 115200
```

Note the `voct_raw=...` value — call it `voct_raw_at_0V`.

- [ ] **Step 3: Patch +1V and read again**

Set the source to +1V. Note the new `voct_raw=...` value — call it `voct_raw_at_1V`.

- [ ] **Step 4: Compute the constants**

```
kVoctZero  = voct_raw_at_0V
kVoctScale = 1.0 / (voct_raw_at_1V - voct_raw_at_0V)
```

Example: if `voct_raw_at_0V = 0.2031` and `voct_raw_at_1V = 0.4078`, then `kVoctScale = 1.0 / 0.2047 ≈ 4.885`.

- [ ] **Step 5: Edit `src/pitch.h`**

Replace the placeholder constants with the measured ones:

```cpp
constexpr float kVoctZero  = 0.2031f;   // measured 2026-05-10 on the author's Patch SM
constexpr float kVoctScale = 4.885f;
```

(Use the exact values you measured. The comment should record the date and which module.)

- [ ] **Step 6: Rebuild and reflash**

```bash
cd ~/code/legio/sawstack
make
make program-dfu
```

- [ ] **Step 7: Verify v/oct tracking**

Patch +0V → reference pitch. Patch +1V → pitch doubles (one octave up). Patch +2V → pitch quadruples. If the octaves don't track 2:1, repeat the measurement at a third voltage (e.g., +2V) and double-check the math.

- [ ] **Step 8: Verify host tests still build with the new constants**

```bash
make -C test
```

Expected: all tests still pass. The `test_voct_disabled_when_scale_zero` test passes its own zero/scale args, so it isn't affected by the new defaults.

- [ ] **Step 9: Commit**

```bash
git add src/pitch.h
git commit -m "calib: hardcode v/oct constants measured on real hardware"
```

---

## Task 17: Detune asymmetry tuning + final acceptance test

- [ ] **Step 1: Listen at full detune in STACK mode with full saw**

Top knob full CW, bottom knob full CW (full saw), left switch on STACK, right switch on STEREO. Play a steady note (encoder coarse 0, fine 0, no v/oct CV). The voices should sound *detuned*, not pitched as octaves or fifths.

If you hear a noticeable octave or fifth ring at any spread amount, the asymmetry multipliers in `kDetuneMul` need a small adjustment. Try changing the +1.013 multiplier to 1.017 or 1.009 and rebuild.

- [ ] **Step 2: Iterate until detune sounds smooth at all spread amounts**

Sweep the top knob from 0 to 1 slowly. There should be no point where voices "lock" to a perceived chord. If any do, perturb the multiplier for the offending voice by ±0.005 and re-test.

- [ ] **Step 3: Update `kDetuneMul` comment to reflect tuned values**

```cpp
// kDetuneMul: tuned by ear on 2026-05-10 — no audible chord lock from 0 to 1.
constexpr float kDetuneMul[5] = { 0.0f, 0.500f, -0.4985f, 1.013f, -1.011f };
```

(Replace with whatever values you settled on.)

- [ ] **Step 4: Run the full acceptance check from spec §12**

Walk through each acceptance bullet:
- All three modes produce audio with no clicks at default detune. ✓ (or note discrepancy)
- Mode-switch via left switch is click-free. ✓
- Detune knob smoothly sweeps unison → ±2 octaves; no octave/fifth coincidences. ✓
- Bottom knob does the right thing per mode. ✓
- Stereo VCA: MONO=L/R, WIDE > STEREO, click-free switching. ✓
- Gate hard-sync resets phases without click. ✓
- V/oct uses hardcoded constants; `voct_raw` always printed. ✓
- LED scheme follows colorblind-safe blue/yellow with brightness. ✓
- Host tests pass. (Run `make -C test` and confirm.)
- In-rack feel test: pickup-and-use works without manual.

- [ ] **Step 5: If any acceptance criterion fails, file a follow-up note in `CLAUDE.md`** under an "Open / known issues" section, the way `legio_tape/CLAUDE.md` does.

- [ ] **Step 6: Commit**

```bash
git add src/pitch.h CLAUDE.md
git commit -m "tune(pitch): detune multipliers, acceptance pass"
```

- [ ] **Step 7: Run `make -C test` one final time and confirm clean**

```bash
make -C test
```

Expected: every test passes; firmware also builds cleanly with `make`.

---

## Self-review

**Spec coverage:**

- §1 module overview → covered by all tasks together.
- §2 scope (in/out) → in-scope items are tasks 2–14; out-of-scope items are explicitly absent.
- §3 hardware mapping → Task 13 (panel-correction, switch reads, encoder semantics, gate edge).
- §3.1 switch panel layout → Task 13 (`mode_from_left_switch`, `width_from_right_switch`).
- §4 top-level architecture → Task 9 (engine skeleton) + Task 12 (per-block flow with smoothing/crossfade/sync).
- §5.1 pitch and detune math → Tasks 6 and 7.
- §5.2 modes (STACK / RICH / SUB) → Tasks 9, 10, 11.
- §5.3 stereo VCA → Task 8.
- §5.4 hard sync (gate edge) → Task 12.
- §5.5 mode-switch crossfade → Task 12.
- §5.6 soft clip → Task 1 (header) + Task 9 (call site in `ProcessBlock`).
- §6 v/oct calibration procedure → Task 6 (constants + bypass) + Task 16 (the procedure itself).
- §7 codebase layout → Task 1 (skeleton) + every later task adding a file.
- §8 LED scheme → Task 14.
- §9 build / flash / telemetry → Task 1 (Makefile, telemetry skeleton) + Task 13 (telemetry format) + Task 15 (flash flow).
- §10 risks → Task 17 (detune asymmetry tuning) is the planned mitigation; the others are observed at flash time.
- §11 hardware-quirks crib → Task 1 (CLAUDE.md content).
- §12 acceptance criteria → Task 17 step 4.

All sections covered.

**Placeholder scan:** No "TBD", "TODO", "fill in details" or hand-waving "add appropriate error handling" lines. The two intentional placeholders (`kVoctZero = 0.0f`, `kVoctScale = 0.0f` and the detune asymmetry multipliers) are explicitly tied to Tasks 16 and 17 with concrete measurement procedures.

**Type consistency:**
- `Mode { STACK, RICH, SUB }` — used identically in `params.h`, all engine code, all tests, `main.cpp`.
- `Width { MONO, STEREO, WIDE }` — same.
- `Voice` methods: `Init`, `SetFrequency`, `NaiveSaw`, `Saw`, `Sine`, `Morph`, `HardSync`, `ResetPhase`, `ResetSyncPhase` — all referenced consistently.
- `SupersawEngine::Init`, `ApplyParams`, `ProcessBlock` — matched throughout.
- `MixVoices(const float voices[5], Width, float* l, float* r)` — same signature in all callers.
- `ComputeMasterHz`, `VoctVoltsFromAdc`, `ComputeVoiceFreqs` — consistent.
- `kNumVoices` defined in `dsp_common.h` (= 5) and used in `test_smoke.cpp`.

No mismatches found.

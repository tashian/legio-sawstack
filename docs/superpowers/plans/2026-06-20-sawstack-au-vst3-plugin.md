# sawstack macOS AU/VST3 Instrument Plugin — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a monophonic macOS AU + VST3 (+ Standalone) supersaw instrument that reuses the sawstack firmware's existing DSP core verbatim, driven by MIDI with an ADSR amp envelope.

**Architecture:** Reorganize the sawstack repo into `firmware/` (current tree, moved wholesale) + `plugin/` (new JUCE project). The plugin compiles the HAL-free DSP `.cpp`s straight from `firmware/src`, wraps `SupersawEngine` in a `juce::AudioProcessor`, drives absolute pitch via one new `external_hz` field on `Params`, and applies a `juce::ADSR` envelope. A JUCE-independent `NoteStack` helper (last-note-priority mono + MIDI→Hz math) is host-unit-tested.

**Tech Stack:** C++17, JUCE 8 (CMake), the existing minimal `test_assert.h` host-test harness, Homebrew `cmake`/`ninja`.

## Global Constraints

- **macOS only.** No Windows/Linux, no cross-platform conditionals.
- **DSP core is shared and HAL-free.** `voice`, `pitch`, `stereo_vca`, `supersaw_engine`, `soft_clip`, `params.h`, `dsp_common.h` must never include `daisy_legio.h`, `daisy_seed.h`, or anything from libDaisy outside `DaisySP/Source/...`. The plugin reuses these files unmodified except for the single `external_hz` addition in Task 2.
- **Dependency direction is one-way:** `plugin/` may depend on `firmware/src` and `firmware/test`; `firmware/` must never reference `plugin/`.
- **Monophonic, last-note priority.** No polyphony in v1.
- **AU identity:** `PLUGIN_MANUFACTURER_CODE Tash`, `PLUGIN_CODE Saws`, AU type music-device (`aumu`), bundle id `com.tashian.sawstack`. (Each 4-char code must contain at least one uppercase letter — JUCE requires it.)
- **Pitch-bend range fixed at ±2 semitones** (not a parameter in v1).
- **C++ standard: C++17** (matches `firmware/test/Makefile`).

---

### Task 1: Reorganize repo into `firmware/` + `plugin/`

Move the entire current tree into `firmware/`, fix submodule paths, confirm host tests still pass. No code logic changes.

**Files:**
- Move: `src/`, `test/`, `Makefile` → `firmware/src/`, `firmware/test/`, `firmware/Makefile`
- Move (submodules): `lib/` → `firmware/lib/`
- Modify: `.gitmodules` (repo root) — submodule paths `lib/...` → `firmware/lib/...`
- Modify: `.gitignore` (repo root) — add `plugin/build/`
- Create: `plugin/.gitkeep` (placeholder so the dir exists; removed in Task 4)

**Interfaces:**
- Produces: `firmware/src/<dsp>.cpp|h` (same relative layout as before), `firmware/test/` host-test build reachable via `make -C firmware/test`.

- [ ] **Step 1: Create the firmware directory and move the source tree**

```bash
mkdir -p firmware plugin
touch plugin/.gitkeep
git mv src firmware/src
git mv test firmware/test
git mv Makefile firmware/Makefile
```

- [ ] **Step 2: Move the submodules**

```bash
git mv lib firmware/lib
```

- [ ] **Step 3: Fix the submodule paths in `.gitmodules`**

Edit `.gitmodules` so it reads exactly:

```ini
[submodule "lib/libDaisy"]
	path = firmware/lib/libDaisy
	url = https://github.com/electro-smith/libDaisy.git
[submodule "lib/DaisySP"]
	path = firmware/lib/DaisySP
	url = https://github.com/electro-smith/DaisySP.git
```

Then sync git's recorded submodule config:

```bash
git submodule sync
git add .gitmodules
```

- [ ] **Step 4: Add the plugin build dir to `.gitignore`**

Append one line to `.gitignore`:

```
plugin/build/
```

- [ ] **Step 5: Verify the submodules are intact after the move**

Run: `git submodule status`
Expected: two lines for `firmware/lib/libDaisy` and `firmware/lib/DaisySP`, each prefixed with a commit hash and **no** leading `-` (a leading `-` means uninitialized). If either shows `-`, run `git submodule update --init firmware/lib/libDaisy firmware/lib/DaisySP`.

- [ ] **Step 6: Verify host tests still build and pass from the new location**

Run: `make -C firmware/test`
Expected: each of `test_smoke test_voice test_pitch test_stereo_vca test_engine` prints `PASS`, ending with `All tests passed.` (The test Makefile uses relative paths `-I../src` / `-I../lib/DaisySP/Source`, which resolve correctly from `firmware/test/`, so no Makefile edit is needed.)

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: move firmware into firmware/, add empty plugin/ for AU/VST3 port"
```

---

### Task 2: Add `external_hz` to drive absolute pitch (shared-DSP change)

Add one defaulted field to `Params` and make `ApplyParams` honor it. Firmware never sets it, so firmware behavior is unchanged. TDD via the existing engine test.

**Files:**
- Modify: `firmware/src/params.h` (add field)
- Modify: `firmware/src/supersaw_engine.cpp` (one line in `ApplyParams`)
- Modify: `firmware/test/test_engine.cpp` (add test + register it)

**Interfaces:**
- Produces: `sawstack::Params::external_hz` (`float`, default `0.0f`). Semantics: `> 0.0f` → used directly as master pitch in Hz; `<= 0.0f` → fall back to the firmware encoder/voct path. Consumed by the plugin in Task 6.

- [ ] **Step 1: Write the failing test**

Add to `firmware/test/test_engine.cpp`, before `run_all()`:

```cpp
static void test_external_hz_overrides_pitch() {
    sawstack::SupersawEngine eng;
    eng.Init(48000.0f);

    sawstack::Params p{};
    p.voct_adc   = sawstack::kVoctZero;  // → 0 V, neutral
    p.width      = sawstack::Width::STEREO;
    p.mode       = sawstack::Mode::STACK;
    p.external_hz = 440.0f;
    eng.ApplyParams(p);
    EXPECT_NEAR(eng.GetMasterHz(), 440.0f, 0.01f);

    // external_hz <= 0 falls back to the encoder/voct path (C4 at zero offsets).
    p.external_hz = 0.0f;
    eng.ApplyParams(p);
    EXPECT_NEAR(eng.GetMasterHz(), 261.63f, 0.5f);
}
```

Register it inside `run_all()` (alongside the existing `RUN_TEST(...)` calls):

```cpp
RUN_TEST(test_external_hz_overrides_pitch);
```

Add `#include "pitch.h"` to `test_engine.cpp` if not already present (needed for `kVoctZero`).

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C firmware/test`
Expected: FAIL — compile error `'external_hz' is not a member of 'sawstack::Params'`.

- [ ] **Step 3: Add the field to `Params`**

In `firmware/src/params.h`, inside `struct Params`, append as the **last** member (after `gate_edge`):

```cpp
    // Plugin-only: absolute master pitch in Hz. <= 0 ignores this and uses the
    // encoder/voct path above. Firmware never sets it (defaults to 0).
    float external_hz = 0.0f;
```

- [ ] **Step 4: Honor the field in `ApplyParams`**

In `firmware/src/supersaw_engine.cpp`, replace this line:

```cpp
    master_hz_ = ComputeMasterHz(coarse_semitones_, fine_cents_, voct_smooth_);
```

with:

```cpp
    master_hz_ = (p.external_hz > 0.0f)
                     ? p.external_hz
                     : ComputeMasterHz(coarse_semitones_, fine_cents_, voct_smooth_);
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make -C firmware/test`
Expected: all tests `PASS`, including `test_external_hz_overrides_pitch`, ending `All tests passed.`

- [ ] **Step 6: Commit**

```bash
git add firmware/src/params.h firmware/src/supersaw_engine.cpp firmware/test/test_engine.cpp
git commit -m "feat(dsp): add Params.external_hz so MIDI can drive absolute pitch"
```

---

### Task 3: `NoteStack` helper (last-note-priority mono + MIDI→Hz), host-tested

A small JUCE-independent class for the glue most prone to bugs. Lives in `plugin/src/`, tested by a dedicated `plugin/test/` target that reuses the firmware harness (`test_assert.h`) via an include path — keeping the dependency direction plugin→firmware.

**Files:**
- Create: `plugin/src/note_stack.h`
- Create: `plugin/src/note_stack.cpp`
- Create: `plugin/test/test_note_stack.cpp`
- Create: `plugin/test/Makefile`

**Interfaces:**
- Produces:
  - `class sawstack::NoteStack` with:
    - `void NoteOn(int note);` — push `note`; it becomes the active note (last-note priority). Re-pressing a held note moves it to active.
    - `void NoteOff(int note);` — remove `note` from anywhere in the stack; active becomes the most-recent remaining note.
    - `bool HasNote() const;`
    - `int ActiveNote() const;` — the active MIDI note, or `-1` if none held.
  - Free function `float sawstack::NoteToHz(int midiNote, float bendSemitones, int coarseSemitones, float fineCents);`
    - = `440.0f * 2^((midiNote - 69 + bendSemitones + coarseSemitones + fineCents/100) / 12)`.
- Consumed by the plugin processor in Task 6.

- [ ] **Step 1: Write the failing tests**

Create `plugin/test/test_note_stack.cpp`:

```cpp
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
```

- [ ] **Step 2: Create the plugin test Makefile**

Create `plugin/test/Makefile`:

```makefile
# plugin/test/Makefile — host-side unit tests for plugin-side glue.
# Reuses the firmware harness (test_assert.h) via include path. Build & run:
#   make -C plugin/test
CXX      ?= g++
CXXFLAGS  = -std=c++17 -O2 -Wall -Wextra \
            -I../src -I../../firmware/test
OUT       = build

TESTS = test_note_stack

all: $(addprefix $(OUT)/, $(TESTS))
	@for t in $(TESTS); do \
	  echo "===== $$t ====="; \
	  $(OUT)/$$t || exit 1; \
	done

$(OUT):
	mkdir -p $(OUT)

$(OUT)/test_note_stack: test_note_stack.cpp ../src/note_stack.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
```

Add `plugin/test/build/` to `.gitignore` (or rely on the existing `build/` pattern — it already matches any-depth `build/`, so no edit needed).

- [ ] **Step 3: Run the tests to verify they fail**

Run: `make -C plugin/test`
Expected: FAIL — `note_stack.h: No such file or directory`.

- [ ] **Step 4: Write `note_stack.h`**

Create `plugin/src/note_stack.h`:

```cpp
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
```

- [ ] **Step 5: Write `note_stack.cpp`**

Create `plugin/src/note_stack.cpp`:

```cpp
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make -C plugin/test`
Expected: all four tests `PASS`, ending `All tests passed.`

- [ ] **Step 7: Commit**

```bash
git add plugin/src/note_stack.h plugin/src/note_stack.cpp plugin/test/test_note_stack.cpp plugin/test/Makefile
git commit -m "feat(plugin): add host-tested NoteStack (mono last-note priority + MIDI->Hz)"
```

---

### Task 4: JUCE scaffold — buildable AU/VST3/Standalone that drones

Add JUCE, a CMake project, and a minimal processor that owns `SupersawEngine` and renders a fixed-pitch drone with default controls. No MIDI, no parameters yet. Proves the build/codepath end-to-end and produces installed plugins. Uses the generic editor.

**Files:**
- Create: `plugin/JUCE/` (git submodule)
- Create: `plugin/CMakeLists.txt`
- Create: `plugin/src/PluginProcessor.h`
- Create: `plugin/src/PluginProcessor.cpp`
- Create: `plugin/src/PluginEditor.h`
- Create: `plugin/src/PluginEditor.cpp`
- Delete: `plugin/.gitkeep`

**Interfaces:**
- Consumes: `sawstack::SupersawEngine` (`Init(float)`, `ApplyParams(const Params&)`, `ProcessBlock(float*, float*, int)`) from `firmware/src`.
- Produces: `SawstackAudioProcessor : juce::AudioProcessor`; `createPluginFilter()` factory; installed `Sawstack.component` (AU), `Sawstack.vst3`, and `Sawstack` Standalone app.

- [ ] **Step 1: Install the CMake toolchain (one-time)**

Run: `brew install cmake ninja`
Expected: `cmake --version` reports 3.22 or newer.

- [ ] **Step 2: Add JUCE as a submodule pinned to a JUCE 8 release tag**

```bash
git submodule add https://github.com/juce-framework/JUCE.git plugin/JUCE
latest8=$(git -C plugin/JUCE tag | grep '^8\.' | sort -V | tail -1)
git -C plugin/JUCE checkout "$latest8"
git add plugin/JUCE .gitmodules
```

Expected: `$latest8` is non-empty (e.g. `8.0.x`); the submodule is now at that tag.

- [ ] **Step 3: Write `plugin/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(Sawstack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

add_subdirectory(JUCE)

set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/src)

juce_add_plugin(Sawstack
    PRODUCT_NAME "Sawstack"
    COMPANY_NAME "Tashian"
    BUNDLE_ID com.tashian.sawstack
    PLUGIN_MANUFACTURER_CODE Tash
    PLUGIN_CODE Saws
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Sawstack PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/note_stack.cpp
    ${DSP_DIR}/supersaw_engine.cpp
    ${DSP_DIR}/voice.cpp
    ${DSP_DIR}/pitch.cpp
    ${DSP_DIR}/stereo_vca.cpp)

target_include_directories(Sawstack PRIVATE src ${DSP_DIR})

target_compile_definitions(Sawstack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Sawstack PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
```

- [ ] **Step 4: Write `plugin/src/PluginProcessor.h`**

```cpp
// plugin/src/PluginProcessor.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "supersaw_engine.h"
#include "note_stack.h"

class SawstackAudioProcessor : public juce::AudioProcessor {
  public:
    SawstackAudioProcessor();
    ~SawstackAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sawstack"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

  private:
    sawstack::SupersawEngine engine_;
    sawstack::NoteStack notes_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SawstackAudioProcessor)
};
```

- [ ] **Step 5: Write `plugin/src/PluginProcessor.cpp`**

```cpp
// plugin/src/PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pitch.h"  // kVoctZero

SawstackAudioProcessor::SawstackAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {}

void SawstackAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine_.Init(static_cast<float>(sampleRate));
}

void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&) {
    const int n = buffer.getNumSamples();

    sawstack::Params p{};
    p.top_adc    = 0.3f;                  // fixed drone defaults for now
    p.bottom_adc = 0.0f;
    p.voct_adc   = sawstack::kVoctZero;   // neutral
    p.mode       = sawstack::Mode::STACK;
    p.width      = sawstack::Width::STEREO;
    p.external_hz = 261.63f;              // fixed C4 drone until Task 6 wires MIDI
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);
}

juce::AudioProcessorEditor* SawstackAudioProcessor::createEditor() {
    return new SawstackAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SawstackAudioProcessor();
}
```

- [ ] **Step 6: Write the editor (generic auto-editor wrapper)**

Create `plugin/src/PluginEditor.h`:

```cpp
// plugin/src/PluginEditor.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SawstackAudioProcessorEditor : public juce::AudioProcessorEditor {
  public:
    explicit SawstackAudioProcessorEditor(SawstackAudioProcessor&);
    void resized() override;
  private:
    juce::GenericAudioProcessorEditor generic_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SawstackAudioProcessorEditor)
};
```

Create `plugin/src/PluginEditor.cpp`:

```cpp
// plugin/src/PluginEditor.cpp
#include "PluginEditor.h"

SawstackAudioProcessorEditor::SawstackAudioProcessorEditor(SawstackAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), generic_(p) {
    addAndMakeVisible(generic_);
    setSize(420, 360);
}

void SawstackAudioProcessorEditor::resized() {
    generic_.setBounds(getLocalBounds());
}
```

- [ ] **Step 7: Remove the placeholder and configure the build**

```bash
git rm plugin/.gitkeep
cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
```

Expected: configuration ends with `-- Generating done` and a `plugin/build/` directory exists. (First configure compiles JUCE's juceaide tool; this may take a minute.)

- [ ] **Step 8: Build all three formats**

Run: `cmake --build plugin/build`
Expected: build succeeds; these appear under `plugin/build/Sawstack_artefacts/Release/`: `AU/Sawstack.component`, `VST3/Sawstack.vst3`, `Standalone/Sawstack.app`. With `COPY_PLUGIN_AFTER_BUILD`, the AU/VST3 are also copied into `~/Library/Audio/Plug-Ins/Components` and `~/Library/Audio/Plug-Ins/VST3`.

- [ ] **Step 9: Sanity-check the Standalone drones**

Run: `open plugin/build/Sawstack_artefacts/Release/Standalone/Sawstack.app`
Expected: the app opens showing the generic editor; you hear a steady C4 supersaw drone (select an audio output device if prompted). Close the app.

- [ ] **Step 10: Commit**

```bash
git add plugin/CMakeLists.txt plugin/src/PluginProcessor.h plugin/src/PluginProcessor.cpp \
        plugin/src/PluginEditor.h plugin/src/PluginEditor.cpp plugin/JUCE .gitmodules
git rm --cached plugin/.gitkeep 2>/dev/null || true
git commit -m "feat(plugin): JUCE scaffold building AU/VST3/Standalone drone"
```

---

### Task 5: Expose parameters via `AudioProcessorValueTreeState`

Add host-automatable parameters and map them into the engine each block. Still no MIDI — the fixed-pitch drone now responds to the parameter knobs in the generic editor.

**Files:**
- Modify: `plugin/src/PluginProcessor.h` (add APVTS + param-layout declaration)
- Modify: `plugin/src/PluginProcessor.cpp` (build layout, read params in `processBlock`, state save/load)

**Interfaces:**
- Produces: `SawstackAudioProcessor::apvts` (`juce::AudioProcessorValueTreeState`) with parameter IDs: `detune`, `morph`, `mode`, `width`, `attack`, `decay`, `sustain`, `release`, `coarse`, `fine`, `level`. (Attack/Decay/Sustain/Release and pitch are wired to audio in Task 6; here they exist and persist.)

- [ ] **Step 1: Declare the APVTS in the header**

In `plugin/src/PluginProcessor.h`, add inside the class (public section) and a static helper:

```cpp
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
```

And in the private section add nothing new yet. Ensure the constructor initializer list (next step) sets `apvts`.

- [ ] **Step 2: Build the parameter layout and wire detune/morph/mode/width/level into the block**

Replace the constructor and `processBlock` in `plugin/src/PluginProcessor.cpp`. Constructor:

```cpp
SawstackAudioProcessor::SawstackAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout
SawstackAudioProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<P>("detune", "Detune", R{0.0f, 1.0f}, 0.3f));
    params.push_back(std::make_unique<P>("morph",  "Morph",  R{0.0f, 1.0f}, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray{"Stack", "Rich", "Sub"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "width", "Width", juce::StringArray{"Mono", "Stereo", "Wide"}, 1));
    params.push_back(std::make_unique<P>("attack",  "Attack",  R{0.001f, 10.0f, 0.0f, 0.3f}, 0.005f));
    params.push_back(std::make_unique<P>("decay",   "Decay",   R{0.001f, 10.0f, 0.0f, 0.3f}, 0.2f));
    params.push_back(std::make_unique<P>("sustain", "Sustain", R{0.0f, 1.0f}, 0.8f));
    params.push_back(std::make_unique<P>("release", "Release", R{0.001f, 10.0f, 0.0f, 0.3f}, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("coarse", "Coarse", -24, 24, 0));
    params.push_back(std::make_unique<P>("fine", "Fine", R{-100.0f, 100.0f}, 0.0f));
    params.push_back(std::make_unique<P>("level", "Level", R{-60.0f, 6.0f}, 0.0f));

    return { params.begin(), params.end() };
}
```

`processBlock` (replacing the Task-4 body):

```cpp
void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&) {
    const int n = buffer.getNumSamples();

    sawstack::Params p{};
    p.top_adc    = apvts.getRawParameterValue("detune")->load();
    p.bottom_adc = apvts.getRawParameterValue("morph")->load();
    p.voct_adc   = sawstack::kVoctZero;
    p.mode  = static_cast<sawstack::Mode>(
        static_cast<int>(apvts.getRawParameterValue("mode")->load()));
    p.width = static_cast<sawstack::Width>(
        static_cast<int>(apvts.getRawParameterValue("width")->load()));
    p.external_hz = 261.63f;  // fixed drone until Task 6
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);

    const float gainDb = apvts.getRawParameterValue("level")->load();
    buffer.applyGain(juce::Decibels::decibelsToGain(gainDb));
}
```

- [ ] **Step 3: Persist plugin state**

Replace the empty `getStateInformation` / `setStateInformation` in `plugin/src/PluginProcessor.cpp`:

```cpp
void SawstackAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void SawstackAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
```

- [ ] **Step 4: Rebuild**

Run: `cmake --build plugin/build`
Expected: build succeeds, no errors.

- [ ] **Step 5: Verify parameters appear and affect the drone**

Run: `open plugin/build/Sawstack_artefacts/Release/Standalone/Sawstack.app`
Expected: the generic editor now shows sliders/combos for Detune, Morph, Mode, Width, Attack, Decay, Sustain, Release, Coarse, Fine, Level. Moving **Detune** thickens the drone; **Mode**/**Width** change its character; **Level** changes loudness. (Attack/Decay/Sustain/Release/Coarse/Fine have no audible effect yet — wired in Task 6.) Close the app.

- [ ] **Step 6: Commit**

```bash
git add plugin/src/PluginProcessor.h plugin/src/PluginProcessor.cpp
git commit -m "feat(plugin): expose detune/morph/mode/width/ADSR/tune/level params via APVTS"
```

---

### Task 6: Wire MIDI, pitch, and the ADSR envelope

The payoff: MIDI notes drive pitch via `NoteStack`, retrigger the saw phases, and an ADSR shapes amplitude. Coarse/Fine/pitch-bend feed the pitch math.

**Files:**
- Modify: `plugin/src/PluginProcessor.h` (add `juce::ADSR`, bend state, last-active tracking)
- Modify: `plugin/src/PluginProcessor.cpp` (`prepareToPlay`, `processBlock` MIDI loop + envelope)

**Interfaces:**
- Consumes: `sawstack::NoteStack`, `sawstack::NoteToHz` (Task 3); `Params.external_hz`, `Params.gate_edge` (Task 2); the APVTS params (Task 5).

- [ ] **Step 1: Add envelope + pitch state to the header**

In `plugin/src/PluginProcessor.h`, add to the private section:

```cpp
    juce::ADSR adsr_;
    juce::ADSR::Parameters adsrParams_;
    float pitchBendSemis_ = 0.0f;   // current bend in semitones (±2)
    int   lastActiveNote_ = -1;     // for retrigger detection
```

- [ ] **Step 2: Initialize the envelope sample rate**

In `plugin/src/PluginProcessor.cpp`, extend `prepareToPlay`:

```cpp
void SawstackAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine_.Init(static_cast<float>(sampleRate));
    adsr_.setSampleRate(sampleRate);
}
```

- [ ] **Step 3: Rewrite `processBlock` to handle MIDI, pitch, and the envelope**

Replace the entire `processBlock` body in `plugin/src/PluginProcessor.cpp`:

```cpp
void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi) {
    const int n = buffer.getNumSamples();

    bool gateThisBlock = false;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            notes_.NoteOn(m.getNoteNumber());
        } else if (m.isNoteOff()) {
            notes_.NoteOff(m.getNoteNumber());
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            notes_ = sawstack::NoteStack{};
        } else if (m.isPitchWheel()) {
            // 14-bit value 0..16383, center 8192 → ±2 semitones.
            pitchBendSemis_ = (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        }
    }

    // Envelope gating from the note stack (last-note priority).
    const int active = notes_.ActiveNote();
    if (active != lastActiveNote_) {
        if (active >= 0) {
            gateThisBlock = true;     // retrigger saw phases on any new active note
            adsr_.noteOn();
        } else {
            adsr_.noteOff();
        }
        lastActiveNote_ = active;
    }

    // Keep ADSR params live from the knobs.
    adsrParams_.attack  = apvts.getRawParameterValue("attack")->load();
    adsrParams_.decay   = apvts.getRawParameterValue("decay")->load();
    adsrParams_.sustain = apvts.getRawParameterValue("sustain")->load();
    adsrParams_.release = apvts.getRawParameterValue("release")->load();
    adsr_.setParameters(adsrParams_);

    // Pitch: hold last frequency if no note is active so release tails stay in tune.
    const int   coarse = static_cast<int>(apvts.getRawParameterValue("coarse")->load());
    const float fine   = apvts.getRawParameterValue("fine")->load();
    float hz = 261.63f;
    if (active >= 0)
        hz = sawstack::NoteToHz(active, pitchBendSemis_, coarse, fine);
    else if (lastActiveNote_ >= 0)
        hz = sawstack::NoteToHz(lastActiveNote_, pitchBendSemis_, coarse, fine);

    sawstack::Params p{};
    p.top_adc    = apvts.getRawParameterValue("detune")->load();
    p.bottom_adc = apvts.getRawParameterValue("morph")->load();
    p.voct_adc   = sawstack::kVoctZero;
    p.mode  = static_cast<sawstack::Mode>(
        static_cast<int>(apvts.getRawParameterValue("mode")->load()));
    p.width = static_cast<sawstack::Width>(
        static_cast<int>(apvts.getRawParameterValue("width")->load()));
    p.external_hz = hz;
    p.gate_edge   = gateThisBlock;
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);

    adsr_.applyEnvelopeToBuffer(buffer, 0, n);

    const float gainDb = apvts.getRawParameterValue("level")->load();
    buffer.applyGain(juce::Decibels::decibelsToGain(gainDb));
}
```

Note: `active >= 0` retrigger detection means `lastActiveNote_` is updated to `active` only when it changes; the pitch `else if (lastActiveNote_ >= 0)` branch is reached during a release tail because by then `active == -1` but the envelope is still ringing — and `lastActiveNote_` was just set to `-1`, so it holds `261.63f`. That is acceptable (a fading tail at C4). If you want the tail to hold the *released* note's pitch instead, that's a future refinement; not required for v1.

- [ ] **Step 4: Rebuild**

Run: `cmake --build plugin/build`
Expected: build succeeds.

- [ ] **Step 5: Play it**

Run: `open plugin/build/Sawstack_artefacts/Release/Standalone/Sawstack.app`
Expected: enable the on-screen/QWERTY keyboard (or attach a MIDI controller). Pressing a key produces a note at the correct pitch; **Attack**/**Release** audibly shape the onset/tail; **Sustain** sets held level; releasing all keys fades to silence. **Coarse**/**Fine** transpose; pitch bend (if controller has a wheel) bends ±2 st. Close the app.

- [ ] **Step 6: Confirm host DSP/glue tests still pass**

Run: `make -C firmware/test && make -C plugin/test`
Expected: both print `All tests passed.`

- [ ] **Step 7: Commit**

```bash
git add plugin/src/PluginProcessor.h plugin/src/PluginProcessor.cpp
git commit -m "feat(plugin): MIDI-driven mono pitch, phase retrigger, and ADSR envelope"
```

---

### Task 7: AU validation, DAW load, and docs

Validate the AU with Apple's `auval`, confirm DAW loading, and document the plugin build alongside the firmware.

**Files:**
- Modify: `CLAUDE.md` (repo root) — describe firmware/plugin split and plugin build/test
- Modify: `README.md` (repo root) — plugin build instructions
- Create: `plugin/README.md` — plugin-specific build/run notes

**Interfaces:**
- Produces: validated, documented plugin. No new code interfaces.

- [ ] **Step 1: Validate the Audio Unit**

Run: `auval -v aumu Saws Tash`
Expected: ends with `* * PASS` (`AU VALIDATION SUCCEEDED`). If macOS reports the component is from an unidentified developer, allow it once in System Settings → Privacy & Security, then re-run. If `auval` does not list the AU at all, run `killall -9 AudioComponentRegistrar` and retry so the registry re-scans.

- [ ] **Step 2: Load in a DAW (manual)**

Open Logic Pro (AU) or Ableton Live (VST3), create an instrument track, insert **Sawstack**, and play. Expected: loads without error, plays in tune, parameters automate. (Manual check — no command output.)

- [ ] **Step 3: Write `plugin/README.md`**

```markdown
# Sawstack plugin (macOS AU / VST3 / Standalone)

Monophonic supersaw instrument. Reuses the firmware DSP core from `../firmware/src`.

## Build

    brew install cmake ninja          # one-time
    git submodule update --init plugin/JUCE
    cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
    cmake --build plugin/build

Artefacts land in `plugin/build/Sawstack_artefacts/Release/` and AU/VST3 are
auto-installed to `~/Library/Audio/Plug-Ins/`.

## Test the host-side glue

    make -C plugin/test

## Validate the AU

    auval -v aumu Saws Tash

## Notes

- Monophonic, last-note priority. ADSR amp envelope. Pitch bend ±2 st.
- The shared DSP is unchanged except `Params.external_hz` (absolute MIDI pitch).
- Polyphony, glide, and a custom UI are intentionally out of scope for v1.
```

- [ ] **Step 4: Update the root `CLAUDE.md`**

Add a section near the top of `CLAUDE.md` (after the opening description) documenting the split:

```markdown
## Repo layout

This repo now holds two front-ends around one shared DSP core:

- `firmware/` — the Legio module firmware (Daisy Patch SM). Build/flash/test as before, but paths are now under `firmware/` (e.g. `make -C firmware/test`, `make -C firmware program-dfu`).
- `plugin/` — a macOS AU/VST3/Standalone instrument (JUCE) reusing `firmware/src` DSP. See `plugin/README.md`.

The DSP core (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`, `soft_clip`)
lives in `firmware/src` and is consumed unmodified by both, except for
`Params.external_hz` which lets the plugin set absolute MIDI pitch (firmware
leaves it 0 and is unaffected).
```

- [ ] **Step 5: Update the root `README.md`**

Add a short "Plugin" section pointing to `plugin/README.md` and noting the macOS AU/VST3/Standalone build. (Mirror the wording from `plugin/README.md`'s Build section; one short paragraph plus the four build commands.)

- [ ] **Step 6: Final verification — both test suites green**

Run: `make -C firmware/test && make -C plugin/test`
Expected: both end with `All tests passed.`

- [ ] **Step 7: Commit**

```bash
git add CLAUDE.md README.md plugin/README.md
git commit -m "docs: document firmware/plugin split and plugin build/validate steps"
```

---

## Self-Review

**Spec coverage:**
- Framework JUCE/CMake → Task 4. ✓
- Repo reorg firmware/+plugin/, plugin pulls DSP from firmware/src → Task 1 + Task 4 CMake `DSP_DIR`. ✓
- `.gitmodules` path update → Task 1 Step 3. ✓
- Monophonic last-note priority + NoteStack host-tested → Task 3, used in Task 6. ✓
- ADSR envelope → Task 6. ✓
- `external_hz` single shared-DSP change, firmware unchanged → Task 2. ✓
- Parameters (detune, morph, mode, width, ADSR, coarse, fine, level) → Task 5. ✓
- Pitch bend ±2 fixed → Task 6 Step 3. ✓
- Generic editor → Task 4 Step 6. ✓
- Build/test three rungs (host tests, Standalone, auval+DAW) → Tasks 4/5/6 (Standalone), Task 3/6 (host), Task 7 (auval+DAW). ✓
- AU codes Tash/Saws/aumu, bundle id → Task 4 CMake + Global Constraints. ✓
- Out of scope (poly, glide, custom UI, signing) → not implemented; noted in plugin/README. ✓

**Placeholder scan:** No TBD/TODO; every code step shows full code; every command has expected output. ✓

**Type consistency:** `NoteStack` methods (`NoteOn`/`NoteOff`/`ActiveNote`/`HasNote`) and `NoteToHz(int,float,int,float)` are defined in Task 3 and called with matching signatures in Task 6. `Params.external_hz` defined Task 2, set Task 4/5/6. APVTS IDs (`detune`,`morph`,`mode`,`width`,`attack`,`decay`,`sustain`,`release`,`coarse`,`fine`,`level`) defined Task 5, read by identical strings in Tasks 5/6. ✓

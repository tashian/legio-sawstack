# sawstack — macOS AU / VST3 instrument plugin

**Date:** 2026-06-20
**Status:** Design approved, ready for implementation plan
**Scope:** Turn the sawstack supersaw DSP into a macOS-only AU + VST3 (+ Standalone) instrument, reusing the firmware's existing DSP core verbatim. Reorganize the sawstack git repo to hold firmware and plugin side by side around the shared DSP.

## Goal

A playable, monophonic supersaw software instrument that loads as an Audio Unit and VST3 in a macOS DAW, driven by MIDI, exposing the same sonic controls as the Legio module (detune, morph, 3-way mode, 3-way width) plus an ADSR amplitude envelope and standard tuning controls. macOS-only; no cross-platform or distribution/notarization requirements for v1.

## Why this is cheap

The DSP core (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`, `soft_clip`) is already HAL-free: it takes/returns `float`, reads a plain `Params` struct, and never includes `daisy_legio.h` / `daisy_seed.h` / anything from libDaisy outside `DaisySP/Source/...`. It already compiles with plain `g++` (that's what the host tests do). The plugin reuses these exact source files; firmware and plugin share one DSP, so a bug fixed once is fixed for both.

The engine is inherently **monophonic** — 5 detuned saws are *one* supersaw voice (plus a sub-octave in SUB mode). The plugin keeps it monophonic (last-note priority), matching the hardware.

## Framework

**JUCE, built via CMake.** A single `juce_add_plugin` target emits **AU + VST3 + a Standalone app** from one codebase, has first-class macOS support, and handles MIDI / parameter / state plumbing. The Standalone target is the primary fast-feedback path — play the synth without a DAW.

Alternatives rejected: iPlug2 (lighter but smaller ecosystem, less turnkey for AU+VST3); hand-rolled AUv3 (most native, most boilerplate, no VST3 path).

## Repo reorganization

`sawstack/` is its own standalone git repo (no remote; the parent `legio/` folder is an unversioned workspace). The reorg happens inside that repo. Chosen layout favors minimal churn: move the entire current tree wholesale into `firmware/`, add a sibling `plugin/` that pulls DSP sources from `../firmware/src`.

```
sawstack/                      (same git repo)
  firmware/                    current tree, moved down one level
    src/        (DSP core + main.cpp + leds)   ← unchanged internals
    test/       host DSP tests                 ← unchanged
    lib/        libDaisy, DaisySP submodules
    Makefile
  plugin/
    CMakeLists.txt             juce_add_plugin → AU + VST3 + Standalone
    src/
      PluginProcessor.{h,cpp}  owns SupersawEngine + juce::ADSR + NoteStack
      PluginEditor.{h,cpp}     generic auto-editor for v1
    JUCE/                      submodule
    (DSP compiled from ../firmware/src — single source of truth)
  docs/superpowers/specs/      this spec
  CLAUDE.md  README.md
```

Mechanical consequences of the move:
- `.gitmodules` submodule paths change `lib/libDaisy` → `firmware/lib/libDaisy` (and DaisySP). Use `git mv` so history is preserved; update `.gitmodules` paths.
- `firmware/Makefile`, `firmware/src/*`, `firmware/test/*` keep identical *relative* paths internally, so no edits to firmware build internals are required.
- Build artifacts (`build/`, `build_host/`) are gitignored; let them regenerate.
- Root `CLAUDE.md` / `README.md` updated to describe the firmware + plugin split.

## Wrapper architecture (`PluginProcessor`)

The processor owns one `SupersawEngine`, one `juce::ADSR`, and a `NoteStack` helper. Per audio block:

1. **MIDI handling.** Walk the block's MIDI buffer. Note-on pushes onto the `NoteStack` (last-note-priority mono); note-off removes it. A new active note → `adsr.noteOn()` and sets `gate_edge = true` for that block (retriggers the 5 saw phases exactly like the hardware gate). When the last held note is released → `adsr.noteOff()`. Pitch-bend events update bend state.
2. **Build `Params`.** `top_adc` = detune param, `bottom_adc` = morph param, `mode`/`width` from choice params, encoder deltas `0`, `voct_adc` = the calibrated v/oct zero (`kVoctZero`, → 0 V, neutral), `gate_edge` per step 1, `external_hz` = target Hz (see DSP change).
3. **Pitch.** Target Hz = note→Hz of `activeNote + pitchBend + coarseTune + fineTune`, handed to the engine via `external_hz`.
4. **Render.** `engine.ProcessBlock(L, R, n)`, then multiply L/R by the ADSR envelope and the output-level gain.

When no note is held and the envelope has finished, output is silent (envelope = 0); the engine may still run (it is cheap) — no special idle path required for v1.

### Testable glue: `NoteStack`

The non-DSP glue most prone to bugs (last-note fallback on release, MIDI-note-and-bend → Hz math) is factored into a small, JUCE-independent `NoteStack` class compiled into the **existing host test build**, so it is unit-tested in seconds like the DSP. JUCE-dependent code (`AudioProcessor` overrides, parameter wiring) stays deliberately thin and is validated by ear / `auval` rather than unit tests.

`NoteStack` responsibilities:
- Push/remove MIDI notes; report the current active note (last-note priority) or "none."
- Report whether a note-on this block should retrigger (new active note vs. legato fallback — v1 retriggers on every new active note).
- Convert `(midiNote, pitchBendSemitones, coarseSemitones, fineCents)` → frequency in Hz.

## Parameters

Host-automatable via `juce::AudioProcessorValueTreeState`:

| Param | Range | Maps to |
|---|---|---|
| Detune | 0..1 | `Params.top_adc` |
| Morph | 0..1 | `Params.bottom_adc` |
| Mode | Stack / Rich / Sub | `Params.mode` |
| Width | Mono / Stereo / Wide | `Params.width` |
| Attack | 1 ms..10 s | `juce::ADSR` |
| Decay | 1 ms..10 s | `juce::ADSR` |
| Sustain | 0..1 | `juce::ADSR` |
| Release | 1 ms..10 s | `juce::ADSR` |
| Coarse tune | ±24 semitones | pitch calc |
| Fine tune | ±100 cents | pitch calc |
| Output level | −∞..+6 dB | output gain |

Pitch-bend range is fixed at ±2 semitones for v1 (not a parameter). UI is JUCE's `GenericAudioProcessorEditor` (auto-generated sliders/combo boxes) — zero custom-UI code, loads and plays immediately. A custom panel is a follow-up; if/when built, follow the project's colorblind-friendly guidance (blue/yellow contrast, never hue alone).

## The one shared-DSP change

The firmware pitch path is *relative* encoder accumulation (`coarse_semitones_ += delta`), unusable for absolute MIDI pitch. Add one **defaulted** field to `Params`:

```cpp
float external_hz = 0.0f;   // <= 0: ignore (use firmware encoder/voct path). > 0: master pitch in Hz.
```

`SupersawEngine::ApplyParams` changes one line:

```cpp
master_hz_ = (p.external_hz > 0.0f) ? p.external_hz : ComputeMasterHz(coarse_semitones_, fine_cents_, voct_smooth_);
```

Because the field defaults to `0.0f` and firmware never sets it, firmware behavior is unchanged. The field is appended at the end of the struct to keep any positional initialization in firmware valid. This is the **only** edit to shared DSP code.

## Build & test

- `plugin/CMakeLists.txt`: `juce_add_plugin(... FORMATS AU VST3 Standalone, IS_SYNTH TRUE, NEEDS_MIDI_INPUT TRUE, COPY_PLUGIN_AFTER_BUILD TRUE, PLUGIN_MANUFACTURER_CODE ..., PLUGIN_CODE ...)`. Add the DSP `.cpp`s from `../firmware/src` to the target's sources; add `../firmware/src` and the DaisySP source path to the include dirs. (DaisySP headers used by the DSP are header/`Source/...` includes already vendored under `firmware/lib/DaisySP`.)
- JUCE added as a git submodule under `plugin/JUCE`, pulled into CMake via `add_subdirectory`.
- Build: `cmake -B build -G Ninja plugin && cmake --build build`. AU → `~/Library/Audio/Plug-Ins/Components`, VST3 → `~/Library/Audio/Plug-Ins/VST3` (auto-installed by `COPY_PLUGIN_AFTER_BUILD`).
- AU 4-char codes: manufacturer `Tash`, plugin code `Saws`, type `aumu` (music device).

**Verification, three rungs:**
1. `make -C firmware/test` still green — DSP unchanged plus new `NoteStack` tests.
2. Launch the **Standalone**, play a MIDI keyboard / on-screen keys, confirm notes sound, ADSR shapes them, detune/morph/mode/width respond.
3. `auval -v aumu Saws Tash` passes; load AU and VST3 in a DAW (e.g. Logic / Ableton via VST3) and play.

Local/unsigned binaries are fine for the user's own machine; Gatekeeper may require a one-time allow.

## Out of scope for v1

Polyphony; glide / portamento; custom UI; mod-wheel / aftertouch routing; preset library; code-signing / notarization for distribution; Windows/Linux.

## Future follow-ups (noted, not built now)

- Polyphony: instantiate N `SupersawEngine`s behind a voice allocator (the engine is already self-contained, so this is additive).
- Custom JUCE editor with the panel layout, colorblind-friendly LEDs/visuals.
- Expose pitch-bend range and add portamento.

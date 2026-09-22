# AGENTS.md — sawstack

A 5-voice **supersaw** for the Noise Engineering Legio module (Daisy Patch SM, STM32H750): 5 detuned saws (or sine↔saw morph / hard-sync / +sub-octave depending on left-switch mode) summed to a stereo VCA. This repo ships **two front-ends around one shared DSP core**:

- `firmware/` — the Legio Eurorack module firmware.
- `plugin/` — a macOS **AU / VST3 / Standalone** instrument (JUCE) that reuses the firmware DSP.

## Repo layout

```
firmware/
  src/      DSP core + main.cpp (HAL) + leds
  test/     host DSP unit tests (plain g++)
  lib/      libDaisy, DaisySP submodules
  Makefile
plugin/
  src/      PluginProcessor/Editor + note_stack (mono voice logic)
  test/     host tests for note_stack (reuses firmware/test/test_assert.h)
  JUCE/     submodule (8.x)
  CMakeLists.txt
docs/superpowers/   firmware + plugin specs and plans
```

The DSP core (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`, `soft_clip`) lives in `firmware/src` and is consumed **unmodified** by both front-ends. The single plugin-facing concession is `Params.external_hz` (> 0 ⇒ absolute master pitch in Hz; firmware leaves it 0 and is unaffected). When editing the DSP core, remember the plugin shares it.

**Design docs:** firmware — `docs/superpowers/specs/2026-05-09-sawstack-supersaw-design.md` / `docs/superpowers/plans/2026-05-10-sawstack-supersaw-firmware.md`. Plugin — `docs/superpowers/specs/2026-06-20-sawstack-au-vst3-plugin-design.md` / `docs/superpowers/plans/2026-06-20-sawstack-au-vst3-plugin.md`. Plans are historical bootstrap artifacts; deviations are recorded at the bottom of each plan and in this file.

## Firmware: build / test / flash

```sh
make -C firmware/lib/libDaisy   # one-time after submodule init
make -C firmware/lib/DaisySP    # one-time
make -C firmware                # firmware → firmware/build/sawstack.bin
make -C firmware/test           # host DSP tests (no hardware)
make -C firmware program-dfu    # flash (BOOT + RESET on the Patch SM submodule first)
```

DFU entry, `Error 74`, and `screen` port contention: see "Legio hardware notes" below.

## Plugin: build / test / validate

```sh
brew install cmake ninja                                   # one-time
git submodule update --init plugin/JUCE                    # one-time (large clone)
cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
cmake --build plugin/build                                 # AU/VST3 auto-install to ~/Library/Audio/Plug-Ins/
make -C plugin/test                                        # host tests for note_stack
auval -v aumu Saws Tash                                    # validate the AU; also lists current params
```

Standalone app: `plugin/build/Sawstack_artefacts/Release/Standalone/Sawstack.app` (fastest by-ear check, no DAW needed).

Plugin behavior: **monophonic, last-note priority**; pitch from MIDI + pitch-bend (fixed ±2 st); `juce::ADSR` amp envelope; phase retrigger on each new note (the firmware "gate"). Host params (8): Detune, Morph, Mode (Stack/Rich/Sub), Width (Mono/Stereo/Wide), Attack, Decay, Sustain, Release. No coarse/fine tune (transpose in host) and no output level (use the track fader). Params are read **once per block** → block-rate modulation, which is fine for DAW LFOs/automation.

Plugin gotchas:
- Only `voice/pitch/stereo_vca/supersaw_engine.cpp` are compiled into the plugin (not `leds.cpp` or `main.cpp`). The DSP needs **no DaisySP include path** — it includes only `<cmath>`, the other DSP headers, and `params.h`.
- `auval` is the strongest automated gate (loads, renders, MIDI, parameter list) — run it after any param change to confirm what shipped.
- **Ableton caches a device instance's parameter strip from the saved set.** After changing the param list, the plugin's own window updates but Ableton's device strip does not — delete the instance and drag in a fresh one (and rescan plugins if needed).
- UI is JUCE's **generic editor** (sliders). A custom knob panel is deferred; note that Ableton's device strip is host-drawn and can never be made knobs regardless of plugin UI.

## Shared DSP / HAL split

DSP modules (`voice`, `pitch`, `stereo_vca`, `supersaw_engine`) **never** include `daisy_legio.h`, `daisy_seed.h`, or anything from libDaisy outside `DaisySP/Source/...`. They take/return `float` and read a plain `Params` struct. This is what makes `make -C firmware/test` work with plain `g++`, what catches algorithm bugs in seconds rather than a flash cycle, and what made the macOS plugin nearly free to build.

The engine API is `Init(float sample_rate)`, `ApplyParams(const Params&)`, `ProcessBlock(float* l, float* r, int n)` (PascalCase). `firmware/src/main.cpp` is the only file that touches the HAL — it reads controls into a `Params` struct, calls `engine.ApplyParams(p)`, and runs `engine.ProcessBlock(out_l, out_r, n)` from the audio callback. The plugin's `PluginProcessor` plays the same role for MIDI/JUCE.

## Legio hardware notes (shared across all Legio firmware in this family)

1. **Legio has 3 ADC channels, not 4.** `CONTROL_KNOB_TOP` and `CONTROL_KNOB_BOTTOM` each read
   **the analog sum of the knob position + the CV jack above it** — they cannot be separated.
   `CONTROL_PITCH` is the dedicated v/oct jack. Treat knob+CV summing as a feature.
2. **`Switch3.Read()` polarity is inverted vs. the panel.** libDaisy returns 0/1/2 =
   CENTER/POS_UP/POS_DOWN, but on Legio's panel `POS_UP=1` is **panel DOWN** and `POS_DOWN=2` is
   **panel UP**. `main.cpp` inverts once so the rest of the code is panel-relative. Verified by feel
   — don't "correct" it.
3. **newlib-nano strips float printf.** `PrintLine("%f", x)` prints nothing unless `-u _printf_float`
   is in `LDFLAGS` (already set in the Makefile, ~10 KB flash). Keep it or float telemetry goes silent.
4. **V/oct needs per-module calibration.** A floating jack reads a non-zero indeterminate ADC value;
   pushing that through `pow(2, volts)` pins pitch at an extreme. Never ship a v/oct path that
   defaults to a wild value — measure 0 V and +1 V on real hardware first, or leave it disabled.
5. **The audio callback is ~1 ms** (48-sample blocks @ 48 kHz). Never call `PrintLine` from inside
   it — it crashes audio. Telemetry goes through a `volatile` UI snapshot read from a slow loop.
6. **Big buffers (delays, loopers) live in SDRAM** via `DSY_SDRAM_BSS` at the point of definition;
   `dsp_common.h` stubs the macro empty for host builds so the same source compiles both ways.

**Flashing.** DFU entry is BOOT + RESET on the Patch SM submodule on the back of the module. After
`make program-dfu`, dfu-util's "Error during download get_status" / `Error 74` is harmless (the
device resets faster than dfu-util can ack), but the module sometimes won't re-enumerate without a
manual reseat. `/dev/cu.usbmodem*` missing right after flashing isn't necessarily a firmware crash.

**Serial.** `screen /dev/cu.usbmodem* 115200`. If `screen` is already attached in another terminal it
holds the device exclusively and `cat`/other readers fail — check `screen -ls`.

## Hardware quirks specific to this app (firmware)

- `Params` uses *panel-relative* switch labels; the Switch3 inversion happens once in `main.cpp`.
- `kVoctScale = 0` until calibration is complete makes the v/oct path a no-op, so the firmware is musically usable from first flash.

## V/oct calibration (firmware)

Defaults for `kVoctZero` / `kVoctScale` live in `firmware/src/pitch.h`; a gitignored `firmware/src/calibration_local.h` (copy the `.example`) overrides them via `__has_include`. The committed defaults were measured on the author's module and will be slightly off on any other unit (typically a few cents to a semitone). Procedure (also in README):

1. Open serial telemetry: `screen /dev/cu.usbmodem* 115200`. Telemetry always prints `voct_raw=<f>`.
2. Patch a known 0 V source to the v/oct jack. Note the printed `voct_raw` → that's `kVoctZero`.
3. Patch a known +1 V source. Note the printed `voct_raw_at_1V`. Compute `kVoctScale = 1.0 / (voct_raw_at_1V - kVoctZero)`.
4. Put both values in `firmware/src/calibration_local.h` (or edit the defaults in `pitch.h`), rebuild, reflash.

`kVoctScale = 0` disables the v/oct path entirely (jack ignored). The plugin sets `voct_adc = kVoctZero` so the v/oct path is inert there.

## Coarse pitch range (firmware)

Encoder press+turn gives ±48 semitones (8 octaves total) of coarse pitch range, centered on C4. The right LED flashes bright white briefly each time you cross an octave boundary so you can navigate by feel. (The plugin ignores the encoder path and drives pitch from MIDI via `Params.external_hz`.)

## Things to avoid

- Don't add a second board class or rewrite the HAL. `DaisyLegio` is complete.
- Don't change the shared DSP in `firmware/src` without remembering the plugin compiles the same files — keep it HAL-free; `Params.external_hz` is the only plugin concession.
- Don't add new test frameworks; `firmware/test/test_assert.h` is intentionally minimal (the plugin tests reuse it).
- Don't claim firmware work is done because host tests pass — the in-rack feel test is the real gate. For the plugin, `auval` passing is not the same as the by-ear/in-DAW play test.

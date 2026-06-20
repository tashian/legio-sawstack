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

Firmware build: `make -C firmware`, `make -C firmware/test` (host tests), `make -C firmware program-dfu` (flash). See CLAUDE.md for full details.

## Plugin

A macOS AU / VST3 / Standalone instrument built with JUCE, reusing the firmware DSP core from `firmware/src`. See [`plugin/README.md`](plugin/README.md) for full details.

    brew install cmake ninja          # one-time
    git submodule update --init plugin/JUCE
    cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
    cmake --build plugin/build

Artefacts land in `plugin/build/Sawstack_artefacts/Release/` and AU/VST3 are auto-installed to `~/Library/Audio/Plug-Ins/`. Validate with `auval -v aumu Saws Tash`.

# legio-sawstack

5-voice supersaw oscillator firmware for the Noise Engineering Legio Eurorack module, plus a macOS AU/VST3 plugin built from the same DSP core.

Part of a family of alternative firmwares for the [Noise Engineering Legio](https://noiseengineering.us/) platform (Daisy Patch SM, STM32H750):
[legio-sawstack](https://github.com/tashian/legio-sawstack) (supersaw oscillator) ·
[legio-stutterer](https://github.com/tashian/legio-stutterer) (stutter → tape delay → DJ filter) ·
[legio-drifter](https://github.com/tashian/legio-drifter) (random Bézier panner / crossfader / CV).
Flash any of them onto a Legio via DFU; the stock firmware can be restored from Noise Engineering's customer portal.

- **Top knob + CV** — Detune (0 = unison → ±2 octaves spread)
- **Bottom knob + CV** — Morph (sine↔saw / hard-sync richness / sub-octave mix, per mode)
- **Left switch** — Mode: STACK / RICH / SUB
- **Right switch** — Stereo VCA: MONO / STEREO / WIDE
- **Encoder rotate** — fine pitch (±50 cents)
- **Encoder press + turn** — coarse pitch (±2 octaves, semitone steps)
- **Pitch jack** — v/oct (see "Calibrating v/oct" below)
- **Gate jack** — hard sync (rising edge resets all voice phases)

Stereo audio out. No internal envelope; this is a pure oscillator.

Firmware build: `make -C firmware`, `make -C firmware/test` (host tests), `make -C firmware program-dfu` (flash). See [`AGENTS.md`](AGENTS.md) for full details, layout, and hardware notes.

## Calibrating v/oct for your module

The Patch SM's v/oct ADC offset and gain vary a little from unit to unit. The two constants in
`firmware/src/pitch.h` (`kVoctZero`, `kVoctScale`) were measured on the author's module; on yours
they will likely be off by a few cents to a semitone. To calibrate:

1. Flash the firmware and open serial telemetry: `screen /dev/cu.usbmodem* 115200`. Every line
   includes `voct_raw=<f>` (the raw 0…1 ADC reading of the pitch jack).
2. Patch a known **0 V** source into the pitch jack (a precision offset/voltage-source module, or a
   DC-coupled output set to 0 V and checked with a meter). Note `voct_raw` → that is `kVoctZero`.
3. Patch a known **+1 V** source. Note `voct_raw` → call it `raw_1v`.
4. `kVoctScale = 1.0 / (raw_1v - kVoctZero)`. Optionally check a third point (e.g. −2 V or +3 V)
   lands within ~1 LSB of `kVoctZero + volts / kVoctScale`; the ADC is linear enough that two points
   suffice.
5. `cp firmware/src/calibration_local.h.example firmware/src/calibration_local.h`, put your two
   values in it, rebuild, reflash. `calibration_local.h` is gitignored, so your values survive
   `git pull` and never end up in a commit. (Editing the defaults in `pitch.h` also works.)

Setting `kVoctScale = 0` disables the v/oct path entirely (the jack is ignored), which is a safe
state while you calibrate. The plugin build is unaffected — it drives pitch from MIDI.

## Plugin

A macOS AU / VST3 / Standalone instrument built with JUCE, reusing the firmware DSP core from `firmware/src`. See [`plugin/README.md`](plugin/README.md) for full details.

    brew install cmake ninja          # one-time
    git submodule update --init plugin/JUCE
    cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
    cmake --build plugin/build

Artefacts land in `plugin/build/Sawstack_artefacts/Release/` and AU/VST3 are auto-installed to `~/Library/Audio/Plug-Ins/`. Validate with `auval -v aumu Saws Tash`.

## License

The source in this repository is MIT — see [`LICENSE`](LICENSE). libDaisy and DaisySP (submodules)
are MIT-licensed by Electrosmith.

The **plugin** links against [JUCE](https://juce.com), which is dual-licensed AGPLv3 / commercial.
Unless you hold a commercial JUCE license, any AU/VST3/Standalone binary you build from `plugin/`
is governed by the AGPLv3 in addition to this repository's MIT terms. The firmware does not use
JUCE and is unaffected.

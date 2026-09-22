# sawstack

5-voice supersaw oscillator firmware for the Noise Engineering Legio module.

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
5. Edit the two constants in `firmware/src/pitch.h`, rebuild, reflash.

Setting `kVoctScale = 0` disables the v/oct path entirely (the jack is ignored), which is a safe
state while you calibrate. The plugin build is unaffected — it drives pitch from MIDI.

## Plugin

A macOS AU / VST3 / Standalone instrument built with JUCE, reusing the firmware DSP core from `firmware/src`. See [`plugin/README.md`](plugin/README.md) for full details.

    brew install cmake ninja          # one-time
    git submodule update --init plugin/JUCE
    cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
    cmake --build plugin/build

Artefacts land in `plugin/build/Sawstack_artefacts/Release/` and AU/VST3 are auto-installed to `~/Library/Audio/Plug-Ins/`. Validate with `auval -v aumu Saws Tash`.

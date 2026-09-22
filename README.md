# legio-sawstack

Alternative firmware for the Noise Engineering Legio Eurorack module. This firmware runs a 5-voice supersaw oscillator. The repository also includes a macOS AU/VST3 plugin, built from the same DSP core.

This firmware is part of a family of alternative firmwares for the Noise Engineering Legio platform (Daisy Patch SM, STM32H750):

- [legio-sawstack](https://github.com/tashian/legio-sawstack) — supersaw oscillator (this firmware).
- [legio-stutterer](https://github.com/tashian/legio-stutterer) — stutter, tape delay, DJ filter.
- [legio-drifter](https://github.com/tashian/legio-drifter) — random Bézier panner, crossfader, and CV source.

You can flash any of these firmwares onto a Legio module through DFU. To restore the stock firmware, use Noise Engineering's customer portal.

## Controls

| Control | Function |
|---|---|
| Top knob + CV | Sets the detune amount. At 0, all voices play in unison. At full, the voices spread up to ±2 octaves. |
| Bottom knob + CV | Sets the morph amount. The effect depends on the mode: sine↔saw blend, hard-sync richness, or sub-octave mix. |
| Left switch | Sets the mode: STACK, RICH, or SUB. |
| Right switch | Sets the stereo VCA width: MONO, STEREO, or WIDE. |
| Encoder, rotate | Sets fine pitch, ±50 cents. |
| Encoder, press and turn | Sets coarse pitch, ±2 octaves, in semitone steps. |
| Pitch jack | V/OCT pitch input. See "Calibrate V/OCT for your module" below. |
| Gate jack | Hard sync input. A rising edge resets the phase of all voices. |

Sawstack outputs stereo audio. It has no internal envelope; this firmware is a pure oscillator.

## Build and flash the firmware

```sh
make -C firmware                 # build
make -C firmware/test            # run host tests
make -C firmware program-dfu     # flash to your module
```

See [`AGENTS.md`](AGENTS.md) for the file layout and hardware notes.

## Calibrate V/OCT for your module

Each Patch SM module has a slightly different V/OCT ADC offset and gain. The constants `kVoctZero` and `kVoctScale`, in `firmware/src/pitch.h`, were measured on the author's module. On your module, these values may be off by a few cents to a semitone. Calibrate your module as follows:

1. Flash the firmware, then open serial telemetry: `screen /dev/cu.usbmodem* 115200`. Every line shows `voct_raw=<f>`, the raw ADC reading of the pitch jack (0 to 1).
2. Patch a known 0 V source into the pitch jack. Use a precision offset or voltage-source module, or a DC-coupled output set to 0 V and checked with a meter. Note the `voct_raw` value. Use this value for `kVoctZero`.
3. Patch a known +1 V source into the pitch jack. Note the `voct_raw` value. Call this value `raw_1v`.
4. Calculate `kVoctScale = 1.0 / (raw_1v - kVoctZero)`. To check your calibration, patch a third voltage, such as −2 V or +3 V. The result must land within about 1 LSB of `kVoctZero + volts / kVoctScale`. The ADC is linear enough that two points are normally sufficient.
5. Copy `firmware/src/calibration_local.h.example` to `firmware/src/calibration_local.h`. Enter your two values. Rebuild and reflash. Git ignores this file, so your values survive `git pull` and never appear in a commit. You can also edit the default values in `pitch.h` directly.

Set `kVoctScale` to 0 to disable the V/OCT path entirely. The jack is then ignored. Use this as a safe state while you calibrate. The plugin build is not affected; it takes pitch from MIDI.

## Plugin

This repository includes a macOS AU / VST3 / Standalone instrument, built with JUCE. The plugin reuses the firmware DSP core from `firmware/src`. See [`plugin/README.md`](plugin/README.md) for full details.

```sh
brew install cmake ninja                                          # one-time
git submodule update --init plugin/JUCE
cmake -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release plugin
cmake --build plugin/build
```

The build places the plugin files in `plugin/build/Sawstack_artefacts/Release/`. The AU and VST3 files install automatically to `~/Library/Audio/Plug-Ins/`. Validate the AU with `auval -v aumu Saws Tash`.

## License

The source in this repository is MIT. See [`LICENSE`](LICENSE). The libDaisy and DaisySP submodules are MIT-licensed by Electrosmith.

The plugin links against [JUCE](https://juce.com), which is dual-licensed AGPLv3 and commercial. If you do not hold a commercial JUCE license, the AGPLv3 license applies to any AU, VST3, or Standalone binary you build from `plugin/`, in addition to this repository's MIT terms. The firmware does not use JUCE, so the firmware is not affected.

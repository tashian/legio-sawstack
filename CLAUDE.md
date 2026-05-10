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

## Coarse pitch range

Encoder press+turn gives ±48 semitones (8 octaves total) of coarse pitch range, centered on C4. The right LED flashes bright white briefly each time you cross an octave boundary so you can navigate by feel.

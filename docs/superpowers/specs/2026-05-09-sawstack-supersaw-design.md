# sawstack — supersaw oscillator on Daisy Patch SM (Legio) — Design

**Status:** Draft for review
**Date:** 2026-05-09
**Target hardware:** Noise Engineering Legio (Daisy Patch SM, STM32H750)
**Reference inspirations:** classic 5-voice analog sawtooth-stack oscillators (timbre, hard-sync richness, multi-octave spread, stereo VCA); Noise Engineering Sinc Legio (control idiom: encoder pitch, mode-select switch, knob-meaning-varies-by-mode).
**New repo:** `~/code/legio/sawstack/` (to be created)

This document specifies a from-scratch firmware that turns the Legio module into a 5-voice supersaw oscillator. It is not a port of `legio_tape`; it is a new sibling project that carries forward the workspace's hardware lessons (HAL, switch polarity, ADC quirks, DFU re-enumeration, newlib-nano printf flag, DSP/HAL split) but otherwise has its own signal chain and UI paradigm.

## 1. What this module is, in one paragraph

A pure-oscillator supersaw module: 5 detunable voices summed to a stereo output. Pitch comes from a v/oct CV jack (calibrated once and hardcoded) plus the encoder for fine and coarse offsets. The top knob/CV controls **detune amount** with a multi-octave maximum spread (±2 octaves at full). The bottom knob/CV controls a **morph** parameter whose meaning depends on the left switch's mode: STACK morphs voice waveshape sine↔saw, RICH applies hard-sync emulation per voice, SUB injects a sub-octave saw. The right switch picks **stereo VCA** width: MONO / STEREO / WIDE. The gate input is a hard-sync trigger that resets all voice phases. There is no internal envelope, no MIDI, and no runtime calibration UI in v1.

## 2. Scope

In scope:

- 5-voice supersaw with three distinct modes (STACK / RICH / SUB).
- Asymmetric detune layout: 2 voices down, 1 center, 2 up; ±2 octaves at full spread; ~1 % asymmetric multipliers prevent voices landing on exact octave/fifth coincidences.
- Stereo VCA at three widths.
- Gate-driven hard sync.
- Hardcoded v/oct calibration with a serial-printed `voct_raw` telemetry field for one-time measurement and any future re-measurement.
- Host-side unit tests for every DSP module.

Out of scope for v1:

- Internal envelope / VCA. (Some hardware saw stacks bundle rise-fall envelopes; we do not.)
- MIDI input.
- Runtime v/oct calibration UI.
- Persistence of any state across power cycles.
- Wavetables (no SDRAM use planned for v1; oscillators are float phase accumulators).
- Per-voice individual outputs (Legio only has stereo).
- A 4th mode.

## 3. Hardware mapping

The Legio's control surface is small. Mapping:

| Legio control                                   | Function                                                                  |
|-------------------------------------------------|---------------------------------------------------------------------------|
| `CONTROL_PITCH` (v/oct jack, dedicated ADC)     | Pitch CV. Uses hardcoded zero/scale constants.                            |
| Encoder rotate                                  | Fine pitch, ±50 cents, ~1 cent/detent.                                    |
| Encoder press + turn                            | Coarse pitch, ±24 semitones, 1 semitone/detent.                           |
| Encoder short press (alone)                     | No-op in v1.                                                              |
| Encoder long press                              | No-op in v1. (Reserved for future use.)                                   |
| `CONTROL_KNOB_TOP` + top CV jack (analog summed)| **Detune amount.** 0 = unison; 1 = ±2 octaves spread.                     |
| `CONTROL_KNOB_BOTTOM` + bottom CV jack (summed) | **Morph.** Per-mode meaning (see §5).                                     |
| Left switch (3-pos, panel-corrected)            | **Mode**: STACK / RICH / SUB.                                             |
| Right switch (3-pos, panel-corrected)           | **Stereo VCA**: MONO / STEREO / WIDE.                                     |
| Gate input                                      | Hard sync. Rising edge → all voice phases reset.                          |

> **Hardware reminder (carried from `legio_tape`).** `Switch3.Read()` returns 0/1/2 = CENTER / POS_UP / POS_DOWN, but on Legio's panel POS_UP corresponds to **panel DOWN** and POS_DOWN to **panel UP**. Whatever code reads the switches must invert the lib's labels to match the player's physical experience. Verified by feel during `legio_tape`. The mapping table above uses *panel-relative* labels.

> **Hardware reminder.** `CONTROL_KNOB_TOP` and `CONTROL_KNOB_BOTTOM` each read the **analog sum** of the knob position and the CV jack above them. They cannot be separated. We treat the summing as a feature: the top CV is "Detune CV" and the bottom CV is "Morph CV," for free.

### 3.1 Switch panel layout

```
 Left ↑  →  STACK         Right ↑  →  MONO
 Left ·  →  RICH          Right ·  →  STEREO
 Left ↓  →  SUB           Right ↓  →  WIDE
```

The center position on the left switch is RICH (not OFF) because there is no OFF state for a pure oscillator — it is always producing audio. The encoder long-press is reserved for future use rather than implementing OFF as a latch.

## 4. Top-level architecture

```
                                 ┌───────── 5 voices ─────────┐
voct + encoder coarse + fine ──→ │ v0  v1  v2  v3  v4         │
detune ────────────────────────→ │ (per-voice freq via §5.1)  │
morph ─────────────────────────→ │ (per-voice waveshape §5.2) │ ──→ stereo VCA ──→ stereo out
gate edge ─────────────────────→ │ (sync resets all phases)   │     (MONO / STEREO / WIDE)
mode ──────────────────────────→ │ (dispatch §5)              │
                                 └────────────────────────────┘
                                                          ↑
                                                    +1 sub voice in SUB mode
```

`SupersawEngine` owns:

- An array of 5 `Voice` instances + 1 sub `Voice` (used only in SUB mode).
- The active mode and the previous mode (for a 5 ms cosine crossfade on switch flick).
- `voct_volts_smoothed`, `detune_norm_smoothed`, `morph_norm_smoothed` one-pole smoothers (~5 ms time constant) to mask ADC jitter.
- A gate-edge detector that compares the current and previous audio-block gate states (rising-edge detection at ~1 ms granularity).

Per-block flow:

1. Update from `Params`: mode, detune, morph, encoder coarse/fine, voct_volts, stereo width.
2. Read the gate input at block start; rising edge versus previous block sets a `sync_pending` flag for this block.
3. Smooth detune/morph/voct.
4. Compute the 5 voice frequencies via `pitch::compute_voice_pitches(...)` (handles encoder + voct + asymmetric detune layout).
5. If `sync_pending`: phase-reset all voices (and sub) to evenly spaced offsets.
6. If mode changed since previous block: apply 5 ms cosine crossfade between previous-mode and current-mode outputs.
7. For each sample in the block: drive each voice (mode-specific waveshape) and the sub (SUB mode only); sum + apply stereo VCA pan.
8. Soft-clip and write to output.

CPU note: 5 polyBLEP saw voices at 48 kHz on STM32H750@480 MHz is comfortable. RICH mode adds a second phase accumulator per voice (the phantom master) plus one comparison per sample — still cheap.

## 5. DSP details

### 5.1 Pitch and detune

Final pitch per voice:

```
master_hz   = 261.63                                  // C4 reference
            * 2 ^ (encoder_coarse_semitones / 12)
            * 2 ^ (encoder_fine_cents      / 1200)
            * 2 ^ (voct_volts)                        // 0 if voct_scale==0

voice_hz[i] = master_hz * 2 ^ (voice_offset_cents[i] / 1200)
```

`encoder_coarse_semitones` ∈ [−24, +24] (4 octaves). `encoder_fine_cents` ∈ [−50, +50]. `voct_volts = (voct_adc - kVoctZero) * kVoctScale`.

Detune knob:

```
detune_norm   = clamp(top_adc, 0, 1)
spread_cents  = detune_norm² * 2400         // 0 → 2400 cents (±2 octaves at edges)
```

Per-voice offsets (in cents from master) — a "2 down / 1 center / 2 up" layout, with ~1 % asymmetric multipliers to avoid landing on exact octave/fifth coincidences at full spread:

| Voice | Offset (cents)                  |
|-------|---------------------------------|
| 0     | 0                               |
| 1     | +0.5 × spread_cents × 1.000     |
| 2     | −0.5 × spread_cents × 0.997     |
| 3     | +1.0 × spread_cents × 1.013     |
| 4     | −1.0 × spread_cents × 1.011     |

The multipliers are first-pass guesses; they are tuned by ear during the first flash session and recorded as a constant.

### 5.2 Modes

All three modes use the 5-voice layout above and the same gate-edge phase reset.

**STACK — sine↔saw timbre crossfade.** Each voice produces `(1 − morph) * sine + morph * polyBLEP_saw` at its assigned frequency. `morph` is the bottom knob, smoothed. At `morph = 0` it sounds like a 5-stack of detuned sines; at `morph = 1` a classic supersaw.

**RICH — hard-sync "richness."** Each voice has two phase accumulators:
- master phase, advancing at `voice_hz`;
- slave phase, advancing at `voice_hz × richness_ratio`.

The audible output is a polyBLEP saw of the slave phase, with a hard-sync reset of the slave to phase 0 every time the master phase wraps. `richness_ratio = 1.0 + morph * 4.0` (1.0 → 5.0). At ratio 1.0 you hear plain saws; as the ratio grows you get the characteristic sync sweep timbre. The sync reset itself uses polyBLEP to suppress aliasing at high ratios.

**SUB — sub-octave injection.** Output is the STACK-mode-with-`morph=1` 5-saw stack (always pure saws), plus a 6th voice: a single polyBLEP saw at `master_hz / 2`, no detune, no stereo panning (always centered). The bottom knob becomes `sub_gain ∈ [0, 1]`, which scales the sub before mix. At `sub_gain = 0` it sounds identical to STACK with morph=1; at `sub_gain = 1` the sub is mixed at the same level as the average main voice.

### 5.3 Stereo VCA (right switch)

Each voice carries a *pan position* derived from its pitch ordering (low → high pitch = left → right).

| Width  | Pan positions (voices in low-to-high pitch order) |
|--------|---------------------------------------------------|
| MONO   | 0, 0, 0, 0, 0    (all voices summed equally to L=R) |
| STEREO | −0.5, −0.25, 0, +0.25, +0.5                       |
| WIDE   | −1.0, −0.5, 0, +0.5, +1.0                         |

Pan law is equal-power: `gainL = cos((pan + 1) * π / 4)`, `gainR = sin((pan + 1) * π / 4)`. The center voice always has equal L and R gain. The sub voice (SUB mode) is always center, regardless of width.

### 5.4 Hard sync (gate edge)

The gate input is read at the start of each audio block (~1 ms granularity). A rising edge versus the previous block triggers a phase reset for the upcoming block: all 5 voices' phase accumulators are reset to evenly distributed offsets `(0, 0.2, 0.4, 0.6, 0.8)`. The sub voice (SUB mode) resets to phase 0. RICH-mode slave phases also reset to 0; master phases reset to the same evenly distributed offsets. Block-rate detection means worst-case sync latency is ~1 ms — inaudible for clocked rhythmic sync, fast enough for kick-driven sync.

The reset is a 1-sample event. polyBLEP saws naturally contain a discontinuity each cycle, so a phase reset blends in acoustically with normal saw behavior — no extra anti-click ramp is needed.

### 5.5 Mode-switch crossfade

Switching modes via the left switch triggers a 5 ms cosine crossfade between the previous-mode block output and the current-mode block output. During the crossfade window both modes' DSP runs in parallel; afterward only the new mode runs. Five milliseconds is enough to suppress audible click on mode change (well below human pop-detection threshold for the saw level differences between modes).

### 5.6 Soft clip

Output is soft-clipped (piecewise tanh-style) before leaving the engine, to keep peaks bounded if the user stacks high detune + WIDE + SUB at full and crosses the headroom budget. Curve borrowed from `legio_tape`'s soft clip.

## 6. V/oct calibration

There is no runtime calibration UI in v1. Calibration is a one-time procedure performed on the user's actual hardware:

1. Build and flash the firmware with placeholder constants in `pitch.h`:
   ```cpp
   constexpr float kVoctZero  = 0.0f;
   constexpr float kVoctScale = 0.0f;     // 0 disables v/oct path
   ```
2. Open serial telemetry. Telemetry always prints `voct_raw=<f>` (the raw normalized ADC reading from the pitch jack, before any zero/scale).
3. Patch a known 0 V source to the pitch jack. Note the printed `voct_raw` value → that is `kVoctZero`.
4. Patch a known +1 V source. Note the printed value, call it `voct_raw_at_1V`. Compute `kVoctScale = 1.0 / (voct_raw_at_1V − kVoctZero)`.
5. Edit `pitch.h` with the two measured constants. Rebuild and reflash.

`kVoctScale = 0` disables the v/oct path entirely (jack is ignored), so the firmware is musically usable from first flash even before calibration. After calibration, v/oct is live.

If the firmware is later run on a different Patch SM module, repeat the procedure. The serial telemetry's `voct_raw` field is permanent, so a re-measurement is always possible.

## 7. Codebase layout

```
sawstack/
├── CLAUDE.md                       Hardware quirks + this app's specifics + deviations.
├── README.md                       Player-facing.
├── Makefile                        APP_TYPE = BOOT_NONE. -u _printf_float in LDFLAGS.
├── lib/
│   ├── libDaisy/                   Submodule.
│   └── DaisySP/                    Submodule.
├── src/
│   ├── main.cpp                    HAL + slow loop. Only file that includes daisy_legio.h.
│   ├── supersaw_engine.{h,cpp}     Top-level orchestrator. Owns voices + mode dispatch + crossfade.
│   ├── voice.{h,cpp}               Single oscillator: phase accumulator, polyBLEP saw, sine,
│   │                               sine↔saw morph, hard-sync emulation, phase reset.
│   ├── pitch.{h,cpp}               Encoder coarse/fine + voct + asymmetric detune layout.
│   │                               Hosts kVoctZero / kVoctScale constants.
│   ├── stereo_vca.{h,cpp}          MONO/STEREO/WIDE pan-law mixer.
│   ├── leds.{h,cpp}                Mode/stereo/boot LED state machine. Gate-edge brightness pulse.
│   ├── soft_clip.h                 Header-only piecewise soft-clip (ported from legio_tape).
│   ├── params.h                    Plain Params struct: mode, top_adc, bottom_adc,
│   │                               left_sw, right_sw, gate_edge, encoder events,
│   │                               voct_adc.
│   └── dsp_common.h                kSampleRate, kAudioBlockSize, DSY_SDRAM_BSS host shim.
└── test/
    ├── Makefile                    Same micro-harness shape as legio_tape/test.
    ├── test_assert.h               Same EXPECT_* macros.
    ├── test_voice.cpp              polyBLEP frequency accuracy, sine↔saw crossfade,
    │                               hard-sync sweep math, phase reset.
    ├── test_pitch.cpp              Encoder semantics, detune layout multipliers,
    │                               voct math (with non-zero scale).
    ├── test_stereo_vca.cpp         Pan law, energy preservation, MONO sums equally,
    │                               WIDE pans more than STEREO.
    ├── test_engine.cpp             Mode dispatch, mode-switch crossfade, gate-edge phase
    │                               reset, sub voice in SUB, richness ratio in RICH.
    └── test_smoke.cpp              Trivial link check.
```

Test discipline (carried from `legio_tape`): `voice`, `pitch`, `stereo_vca`, `leds`, `supersaw_engine` never include `daisy_legio.h`, `daisy_seed.h`, or anything from libDaisy outside `DaisySP/Source/...`. They take and return `float` samples and read a plain `Params` struct. This is what makes `make -C test` work with plain `g++` and what catches algorithm bugs in seconds rather than in a flash cycle.

No SDRAM use in v1: voices are float phase accumulators, no big sample buffers.

## 8. LED scheme

Designed for a colorblind user. Two design moves:

1. Stick to **blue and yellow** as the primary hues (most distinguishable across colorblindness types).
2. Use **brightness** as an independent axis so each indicator stays readable even if hue washes out.

**Left LED — mode indicator** (3 visually distinct appearances):

| Mode  | Color & brightness                              |
|-------|-------------------------------------------------|
| STACK | bright blue (full sat, ~80 % brightness)        |
| RICH  | bright yellow (full sat, ~80 % brightness)      |
| SUB   | dim white (~25 % brightness — clearly "neither blue nor yellow") |

Brightness pulses up briefly (~50 ms) on each gate-sync rising edge so the player can see the sync rate. Pulse keeps the same hue.

**Right LED — stereo width indicator** (one hue, ramped brightness — width = brightness is intuitive):

| Width  | Color & brightness          |
|--------|-----------------------------|
| MONO   | barely on (~5 % brightness) |
| STEREO | blue at ~40 % brightness    |
| WIDE   | blue at ~90 % brightness    |

**Boot identification** (first 500 ms after power-on): both LEDs alternate blue ↔ yellow at 4 Hz (two complete blinks). Visually distinct from `legio_tape`'s boot pattern; uses only the two firmware-signature hues.

## 9. Build, flash, telemetry

### 9.1 Build

```sh
make -C lib/libDaisy   # one-time after submodule init
make -C lib/DaisySP    # one-time
make                   # firmware → build/sawstack.bin
make -C test           # host DSP tests (no hardware)
```

### 9.2 Flash

The Patch SM ships with no bootloader, but `BOOT_NONE` doesn't need one — the firmware lives directly in the 128 KB internal flash.

```sh
make program-dfu       # flashes the firmware (module must be in DFU mode first)
```

DFU entry: BOOT + RESET on the Patch SM submodule (back of the module). The user can't always physically do this without disturbing the patch — be patient at the flash gate. After flashing, `dfu-util`'s `Error 74 / Error during download get_status` message is harmless (carried lesson from `legio_tape`); a manual reseat is sometimes needed if the device doesn't re-enumerate.

`-u _printf_float` stays in `LDFLAGS` (~10 KB flash cost, but float telemetry would otherwise go silent under newlib-nano).

### 9.3 Serial telemetry

Volatile `UiSnapshot` updated from the audio callback, drained by the slow loop at 5 Hz, printed via `hw.seed.PrintLine`. Format:

```
mode=STACK pitch=440.0Hz det=1247c morph=0.62 stereo=WIDE sync=42 voct_raw=0.5031
```

`voct_raw` is always present so the v/oct calibration constants can be re-measured at any time without changing firmware.

`PrintLine` is never called from the audio callback (~1 ms block budget) — that crashes audio. Telemetry goes through the volatile snapshot pattern from `legio_tape`.

## 10. Risks and known unknowns

1. **Hard-sync aliasing at high richness ratios.** At ratio ~5.0 the slave saw has lots of high-frequency content. polyBLEP on the inner saw and on the sync reset is the plan. If aliasing is bad on hardware, cap ratio at ~4.0 or precompute a small antialiased lookup. Test on hardware before locking the ratio range.

2. **Detune asymmetry multipliers** (1.000 / 0.997 / 1.013 / 1.011) are first-pass guesses. They prevent voices landing on exact octave/fifth coincidences at full spread, but the actual values need feel-testing. Tune by ear during the first flash session and record as constants.

3. **CPU headroom.** 5 polyBLEP voices in stereo is comfortable on H750. SUB adds a 6th voice; RICH doubles per-voice phase math. Profile early — before locking the morph ranges.

4. **Mode-switch crossfade duration.** 5 ms is a guess; if mode flicks click in practice, extend to 10–20 ms.

5. **Gate edge debouncing.** The Legio gate input may have edge bounce on certain sources. Reuse `legio_tape::Clock`'s edge-detect with hysteresis if needed; don't over-engineer if the simple rising-edge detector works.

6. **No SDRAM means no future wavetables.** If a v2 mode wants wavetable-based shapes, that's a project-shape change.

## 11. Hardware-quirks crib (carried verbatim from `legio_tape`)

These cost time during the first flash session of `legio_tape`. They will cost the same time on `sawstack` if forgotten. They go straight into `sawstack/CLAUDE.md`.

1. **3 ADC channels, not 4.** `CONTROL_KNOB_TOP` and `CONTROL_KNOB_BOTTOM` each read the **analog sum** of their knob and the CV jack above. They cannot be separated. `CONTROL_PITCH` is the v/oct jack on its own ADC.
2. **Switch3 polarity is inverted on Legio.** `POS_UP=1` corresponds to **panel DOWN**; `POS_DOWN=2` corresponds to **panel UP**. Verified by feel.
3. **newlib-nano strips float printf.** Add `-u _printf_float` to `LDFLAGS` (~10 KB flash cost) or float telemetry goes silent.
4. **DFU re-enumeration sometimes needs a manual reseat.** dfu-util's `Error 74` is harmless; the device often won't re-enumerate to the host without a brief power-cycle.
5. **`screen` on the same port blocks `cat`.** If you can't read serial, check `screen -ls`.
6. **Audio callback is ~1 ms (48-sample blocks @ 48 kHz).** Never call `PrintLine` from inside it. Telemetry goes through a volatile snapshot drained by the slow loop.
7. **Floating v/oct jack reads as a non-zero indeterminate ADC value.** Through `pow(2, volts)` this pins pitch at extremes. The fix here is `kVoctScale = 0` until calibration is done, which makes the v/oct path a no-op.

## 12. Acceptance criteria for v1

- All three modes (STACK / RICH / SUB) produce audio with no clicks at default detune.
- Mode-switch via the left switch is click-free (5 ms cosine crossfade).
- Detune knob smoothly sweeps unison → ±2 octaves. Per-voice pitches don't land on exact octave/fifth coincidences at full spread (verified by ear and by `test_pitch.cpp`).
- Bottom knob does the right thing per mode: sine→saw crossfade in STACK, hard-sync sweep in RICH, sub-mix in SUB.
- Stereo VCA: MONO outputs identical L=R; WIDE pans noticeably more than STEREO; switching is click-free.
- Gate hard-sync resets all voice phases on rising edge with no audible click from the reset itself.
- V/oct path uses hardcoded `kVoctZero` / `kVoctScale` measured once on real hardware. Telemetry prints `voct_raw` so the constants can be re-measured if needed.
- LED scheme follows the colorblind-safe blue/yellow palette with brightness as a secondary axis, including boot identification, mode indication, stereo width, and gate-edge brightness pulse.
- Host tests pass for every module.
- In-rack feel test (the real gate, per the carried `legio_tape` discipline): pick up the module, plug a clock + audio, get a usable supersaw without reading a manual.

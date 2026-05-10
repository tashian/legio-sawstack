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

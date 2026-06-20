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

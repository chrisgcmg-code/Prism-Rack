# Prism Rack

A four-module multi-effect VST3 for Ableton Live, modelled on the workflow of the
Hologram Chroma Console. For personal use only.

**Signal chain:** Character → Movement → Diffusion → Texture (reorderable), then Mix and Output.

| Module | Effects | Big knobs | Small knob |
| --- | --- | --- | --- |
| Character (4× oversampled) | Drive, Sweeten, Fuzz, Howl, Swell | Tilt, Amount | Sensitivity |
| Movement | Doubler, Vibrato, Phaser, Tremolo, Pitch | Rate, Amount | Drift |
| Diffusion | Cascade, Reels, Space, Collage, Reverse | Time, Amount | Drift |
| Texture (2× oversampled) | Filter, Squash, Cassette, Broken, Interference | Mix, Amount | Output |

**SETUP** opens the extra controls: per-module FX Vol and Always On, Texture Color and Filter mode,
Reverse speed, Trails, Drift Macro, tempo source (Free / Host / Tap) and Tap BPM.

## Build on Windows (about 15 minutes the first time)

1. Install **Visual Studio 2022 Community** and choose the **"Desktop development with C++"** workload.
   That includes CMake. Also install **Git for Windows**.
2. Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu, `cd` into this folder, and run:

   ```
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --target PrismRack_VST3
   ```

   The first configure downloads JUCE 8.0.4 automatically (needs internet).
3. Right-click **install-windows.bat** → **Run as administrator**. It copies `Prism Rack.vst3` to
   `C:\Program Files\Common Files\VST3`.
4. In Ableton: **Options → Preferences → Plug-Ins**, turn on **Use VST3 Plug-In System Folders**, click **Rescan**.
   Prism Rack appears under Plug-Ins → Chris Gilling.

Alternative: open the folder in Visual Studio (File → Open → Folder), pick the *x64-Release* configuration
and build the `PrismRack_VST3` target.

No Visual Studio? Push this folder to a GitHub repository. The included workflow
(`.github/workflows/build-windows.yml`) builds the VST3 on GitHub's Windows machines;
download it from the run's **Artifacts** section.

## Tips for ukulele and synths

- Use **Headroom → AUTO** and play normally for 5 seconds. It picks Low/Medium for a uke pickup and
  High/Very High for synth VSTs. Each Live preset remembers its own setting.
- Put Prism Rack on a synth track after the instrument, or on a uke audio track with monitoring on.
- Double-click any knob to reset it. Click the `<` `>` arrows above a module to move it earlier or later in the chain.
- Tempo defaults to **Host** so delays and LFOs follow Live's tempo. Switch to **Free** in SETUP for
  unsynced times, or hit **TAP** for tap tempo.

## Testing

The `PrismRenderTest` app (built by default) renders every effect at several settings through
a ukulele-like and a synth test signal and checks for NaNs, runaway levels, silence, bypass
accuracy, pitch accuracy and CPU use:

```
cmake --build build --config Release --target PrismRenderTest
build\PrismRenderTest_artefacts\Release\PrismRenderTest.exe  [optional folder for WAV renders]
```

Verified on Linux: all 40 effect/source combinations pass, bypass nulls to 7e-9, Pitch +12 measures
873 Hz for a 440 Hz input, CPU about 2 % of one core at 48 kHz / 128 samples, total latency 10 samples
(0.2 ms). The VST3 passes pluginval at strictness 10, including the editor tests.

## Project layout

```
src/Parameters.h          parameter IDs, ranges, defaults (IDs are stable: automation depends on them)
src/PluginProcessor.*     chain order, headroom, mix, bypass, tempo, state
src/Module.h              one slot: 5 effects, switching crossfade, bypass, oversampling, FX Vol
src/dsp/Core.h            filters, delay lines, LFOs, envelopes, smoothing, random
src/effects/*.h           the 20 effects
src/PluginEditor.*        pedal-style UI;  src/ui/Theme.h  colours, knob drawing, dark mode
tests/RenderTest.cpp      offline tests and UI snapshots (--snapshot <prefix>)
```

## Not in v1 (planned)

Gesture recording, Capture looper/sustainer, factory presets, MIDI CC learn.

# FluxFilter — Concept & v1 Plan

## Context
New experimental JUCE filter plugin (VST3/AU/Standalone). Goal: deep, organic sound with a strong sound-design focus and lots of modulation. Repo is a fresh JUCE skeleton (`src/PluginProcessor.*`, `src/PluginEditor.*`, `CMakeLists.txt`). Decisions so far: **filter field** core, **full mod matrix**, sources = **LFOs with drawable shapes, envelope follower, chaos/drift**, v1 = **sound first, simple UI**.

## The idea in one sentence
Instead of one filter that moves, FluxFilter is a **field of resonant cells spread across the spectrum**, and modulation doesn't hit them all at once — it **flows through the field like a wave through water**, so movement smears, ripples and blooms across frequencies.

## 1. Signal flow
```
in → Input Drive → [ FLUX FIELD: N resonant cells, each with own cutoff/Q/gain/pan ]
                      ↑ field shaped by: Tilt · Spread · Center · Wave (Flux) · Viscosity
   → Cell Feedback loop (with Damping + soft saturation) → Mix/Dry-Wet → Output
```
- **Cells:** N = 4–24 (param "Density"), each a TPT/ZDF state-variable filter (stable under fast modulation) with per-cell morph LP↔BP↔HP↔Notch. Default BP = "vocal/resonant body" sound.
- **Cell placement:** `Center` (Hz) + `Spread` (octaves) + `Scale` (log / harmonic series / musical scale / inharmonic "bell" ratios). Harmonic & bell scales give the deep, physical, tuned-body character.
- **Combine mode:** Parallel (sum, rich formant-like) / Serial (cascade, dark & steep) / Blend.
- **Stereo:** cells alternate or spread L/R ("Width"), plus a stereo phase offset for the flux wave.

## 2. The "Flux" — what makes it move
Each modulation value is applied to the field as a function of **cell position** `x ∈ [0,1]`:
- **Wave Speed / Direction:** how fast modulation travels from low to high cells (or high→low, or outward from center). Implemented as a per-cell delay line on the modulation signal (cheap: control-rate ring buffer).
- **Viscosity:** per-cell smoothing/lag — high = syrupy, slow blooms; low = sharp ripples.
- **Wavelength:** how many "peaks" across the field at once (sine-shaped spatial distribution vs uniform).
- **Turbulence:** small per-cell random offsets (seeded, smooth) so cells never move in perfect lockstep → organic.

## 3. Organic / deep character (the secret sauce)
- **Analog drift:** each cell has a slow brownian cutoff wobble (subtle, on by default, amount knob).
- **Nonlinear resonance:** tanh/asymmetric soft clip inside each SVF integrator loop → resonance thickens and "growls" when driven, never harsh.
- **Global feedback** around the whole field with a damping lowpass → can self-oscillate into tuned drones/bodies.
- **Energy compensation:** auto-gain by density & resonance so it stays usable while experimenting.
- **Character switch:** Clean / Warm / Broken (adds bit-crush-ish quantization of cell cutoffs for experimental grit).

## 4. Modulation system (full matrix)
**Sources (v1):**
| Source | Details |
|---|---|
| LFO 1–3 | Tempo-sync or Hz, drawable curve (points + curvature, like Vital), phase, retrigger, unipolar/bipolar |
| Envelope Follower | Attack/release, sensitivity, plus split into **Transient** and **Body** outputs (two sources) |
| Chaos | Lorenz attractor (X/Y as two sources), speed, character |
| Drift | Smooth brownian / sample&hold with slew |
| Macros 1–4 | Knobs the user maps to many targets |

**Matrix slot:** source → target, depth (bipolar), curve (lin/exp/S), **Flux on/off** (whether this slot travels through the field or hits all cells at once — key creative switch), optional via-source (mod-of-mod depth).
- ~16 slots; any automatable parameter is a target, including other mod sources' rates/depths.
- Modulation computed at control rate (every 32 samples) with per-sample smoothing of filter coefficients.

## 5. Parameters (v1 list)
Field: Density, Center, Spread, Scale, Tilt, Resonance, Cell Morph, Combine Mode, Width
Flux: Wave Speed, Direction, Viscosity, Wavelength, Turbulence
Character: Drive, Feedback, Damping, Drift Amount, Character mode
Output: Mix, Output Gain
Mod: LFO1–3 (rate, sync, shape, phase), Env (atk, rel, sens), Chaos (speed), Drift (rate, slew), Macros 1–4, 16 matrix slots

## 6. Code structure (to build)
```
src/
  PluginProcessor.*        // APVTS, process loop, state save/load
  PluginEditor.*           // v1: simple layout of knobs + matrix table
  dsp/FluxCell.h           // nonlinear ZDF SVF with morph
  dsp/FluxField.h/.cpp     // cell array, placement scales, combine modes, feedback
  dsp/FluxPropagator.h     // per-cell mod delay + viscosity smoothing + turbulence
  mod/LFO.h, EnvelopeFollower.h, ChaosLorenz.h, Drift.h
  mod/ModMatrix.h/.cpp     // slots, routing, control-rate evaluation
  params/Parameters.h      // all parameter IDs + layout in one place
```
Use JUCE `AudioProcessorValueTreeState`, `juce::dsp` utilities (SmoothedValue, ProcessSpec); write SVF ourselves (JUCE's StateVariableTPTFilter has no nonlinear loop). Add new files to `target_sources` in `CMakeLists.txt` and link `juce::juce_dsp`.

## 7. Build phases
1. **Core field:** FluxCell + FluxField with static params; hear it, tune the sound.
2. **Organic layer:** drift, nonlinear resonance, feedback, auto-gain.
3. **Flux propagation:** wave speed / viscosity / turbulence with one internal LFO.
4. **Mod sources + matrix:** LFOs (with shape data), env follower, chaos, drift, macros, 16 slots, state save/load.
5. **Simple UI:** grouped knobs, a live field visualizer (bars per cell showing cutoff/level — cheap and very useful), matrix as a table, basic curve editor for LFOs.
6. **Polish:** presets, CPU optimizations (SIMD cells, control-rate tuning), pluginval.

## Verification
- Build in CLion / `cmake --build cmake-build-debug`, run Standalone with test audio (noise, drums, pads).
- Check stability: max resonance + feedback + fast modulation, no NaN/blowups (add debug assertion on output).
- CPU check at 24 cells, 96 kHz.
- `pluginval --strictness-level 5` on VST3 + AU and `auval`, as with audible_spec; test in Ableton Live 12.
- Automation + state recall (save/reload project, mod matrix restored).

## Open later (not v1)
Step sequencer / MIDI keytrack to play the field, custom animated UI, spectral "freeze" of the field, sidechain input as env source.

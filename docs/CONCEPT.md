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

## 2. The "Flux" — what makes it move (built in phase 3)
A source signal (for now the built-in **Flux LFO**: Rate, Shape Sine/Triangle/Ramp/Random) enters the field at one end and travels through it. Each cell reads it from a control-rate delay line at its own position on the path (`FluxPropagator`, generic so the phase 4 matrix can reuse it for any source).
- **Flux Depth** (± octaves): how far the wave moves the cutoffs. **Flux Level** (0..1): the wave also swells/ducks each cell's loudness.
- **Travel** (0–4 s): time for the wave to cross the field. 0 = all cells move together.
- **Direction:** Up, Down, Outward (from the center), Inward (towards the center).
- **Viscosity:** lag that grows along the path, so the wave smears and slows as it travels.
- **Wavelength:** spatial cycles across the field. 0 = all cells move the same way; 0.5 = the two ends move in opposition; higher = cells breathe towards/away from each other.
- **Turbulence:** every cell gets its own slowly changing arrival time and depth, so the wave never moves in lockstep.
- **Flux Stereo:** the right channel hears the wave up to 250 ms later.

## 3. Organic / deep character (built in phase 2)
- **Drift:** every cell (and L/R separately) wanders on its own slow random walk that always returns home, up to ±1/4 octave.
- **Character:** saturation inside each cell's resonance, set in output units so it behaves the same at any Q.
  - *Clean*: bends only at very high levels.
  - *Warm*: bends around 0 dBFS, the negative half earlier → even harmonics.
  - *Broken*: bends early and hard, cutoffs snap to a quarter-octave grid and update in steps (every 256 samples).
- **Feedback + Damping:** the wet signal goes back into the field through a lowpass and a soft clipper. Above ~0.9 the field self-oscillates into tuned drones.
- **Auto Gain:** matches the wet loudness to the dry input (±18 dB, ~300 ms). It holds its value while the input is silent, so tails and drones are not pumped up.
- **Safety:** DC blocker on the wet path, a soft +12 dBFS wet ceiling that is always on, and a switchable soft limiter at 0 dBFS (on by default).

## 4. Modulation system (built in phase 4)
**Sources:**
| Source | Details |
|---|---|
| Flux LFO | The phase 3 wave LFO, also usable in the matrix |
| LFO 1–3 | Hz or tempo sync (4 bars … 1/32, triplets, dotted; locked to the host position while playing), shapes Sine, Triangle, Saw Up/Down, Square, Random, **Custom** (drawable: points + curvature, saved with the project; drawing UI in phase 5), phase offset, bipolar/unipolar |
| Envelope / Transient / Body | Follower on the dry input, 60 dB scale. Envelope = user attack/release; Body = slow sustain; Transient = how far the level jumps above the body (3 dB threshold, silent on steady material) |
| Chaos X / Y | Lorenz attractor; Speed (0.05–20× time) and Shape (calmer → wilder) |
| Drift | Smooth random walk, or Sample & Hold with slew |
| Macros 1–4 | Plain knobs to map many targets |

**Matrix: 16 slots**, each with Source → Target, Depth (−1..1), Curve (Linear, Exp, Log, S-Curve), **Flux** switch and **Via** (scales the slot by another source).
- Depth is in the target's knob range: depth 0.2 on Cutoff moves the cells as far as turning Center by 20 %.
- **Per-cell targets:** Cutoff, Resonance, Morph, Level. With **Flux on**, the slot's movement enters the field and travels through it like the flux wave (same Travel, Direction, Viscosity, Wavelength, Turbulence, Stereo). Flux-on slots per target are summed and share one propagator.
- **Global targets:** Spread, Tilt, Parallel/Serial, Width, Drift, Feedback, Damping, all Flux controls, Drive, Mix, Output, LFO 1–3 rate, Chaos speed, Drift rate.
- Everything runs at control rate (every 32 samples). Modulation is added after parameter smoothing, so fast LFOs are not smeared.
- Ranges of all continuous parameters live in `params/ParamSpec.h`, shared by the host parameters and the matrix.

## 5. Parameters (v1 list)
Field: Density, Center, Spread, Scale, Tilt, Resonance, Cell Morph, Combine Mode, Width
Flux: Wave Speed, Direction, Viscosity, Wavelength, Turbulence
Character: Drive, Feedback, Damping, Drift, Character mode, Auto Gain, Limiter
Output: Mix, Output Gain
Mod: LFO1–3 (rate, sync, division, shape, phase, polarity), Env (attack, release, sensitivity), Chaos (speed, shape), Drift (rate, mode, slew), Macros 1–4, 16 matrix slots × (source, target, depth, curve, flux, via) — 154 parameters in total

## 6. Code structure (to build)
```
src/
  PluginProcessor.*        // APVTS, process loop, state save/load
  PluginEditor.*           // v1: simple layout of knobs + matrix table
  dsp/FluxCell.h           // nonlinear ZDF SVF with morph
  dsp/FluxField.h/.cpp     // cell array, placement scales, combine modes, feedback
  dsp/FluxPropagator.h     // per-cell mod delay + viscosity smoothing + turbulence
  mod/CurveShape.h         // drawable LFO shape + lock-free hand-over to the audio thread
  mod/ModSources.h         // LFO, envelope follower, Lorenz chaos, drift
  mod/ModMatrix.h          // slots, routing, flux propagation, control-rate evaluation
  params/ParamSpec.h       // ranges of all continuous parameters (shared with the DSP)
  params/Parameters.h      // IDs, choices, groups, JUCE layout
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

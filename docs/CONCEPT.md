# FluxFilter — Concept & v1 Plan

> **Superseded for the sound engine by [CONCEPT_V2.md](CONCEPT_V2.md)** (formant field, life: drift, coupling, life cycle, rasp, pitch anchor, stereo life). The UI, modulation, output and preset sections below still describe the shared parts.

## Context
New experimental JUCE filter plugin (VST3/AU/Standalone). Goal: deep, organic sound with a strong sound-design focus and lots of modulation. Repo is a fresh JUCE skeleton (`src/PluginProcessor.*`, `src/PluginEditor.*`, `CMakeLists.txt`). Decisions so far: **filter field** core, modulation by **drag and drop** (phase 5b replaced the matrix table), sources = **three analog-style LFOs**, one panel UI with the **Flux** knob at its centre.

## The idea in one sentence
Instead of one filter that moves, FluxFilter is a **field of resonant cells spread across the spectrum**, and modulation doesn't hit them all at once — it **flows through the field like a wave through water**, so movement smears, ripples and blooms across frequencies.

## 1. Signal flow
```
in → Input Drive → [ FLUX FIELD: N resonant cells, each with own cutoff/Q/gain/pan ]
                      ↑ field shaped by: Spread · Center · Wave (Flux) · Viscosity
   → Cell Feedback loop (with Damping + soft saturation) → Mix/Dry-Wet → Output
```
- **Cells:** N = 1–16 (param "Density"), each two TPT/ZDF state-variable filters in series (24 dB/oct skirts, stable under fast modulation) with per-cell morph LP↔BP↔HP↔Notch. Default BP = "vocal/resonant body" sound.
  - Bandpass: both stages resonate at the full Q, so bands are narrow and the cells clearly remove what lies between and outside them (defaults on a sine: between neighbouring cells about −13 dB below a cell, outside the field −44 to −60 dB; one stage per cell gave only −21 to −26 dB outside, which sounded like an EQ boost rather than a filter).
  - LP / HP / notch: the second stage is a plain Butterworth stage, and the resonant bump is tamed to √Q, so high resonance makes a lowpass thinner instead of +30 dB louder.
- **Level of the field:** the field never amplifies a tone. Several hundred times a second it computes its own loudest point (all cell centres plus beyond both ends, magnitudes summed) for the current cutoffs, Q and morph, and scales the sum so that point is at most 0 dB. The old fixed makeup gave up to +21 dB on a tone sitting on a cell (whistling peaks, clipping in Live).
- **Cell placement:** `Center` (Hz) + `Spread` (octaves), spaced logarithmically. (The DSP also has harmonic, pentatonic and inharmonic "bell" spacings; since phase 5b they have no parameter.)
- **Combine mode:** Parallel (sum, rich formant-like) / Serial (cascade, dark & steep) / Blend.
- **Stereo:** cells alternate or spread L/R ("Width"), plus a stereo phase offset for the flux wave.

## 2. The "Flux" — what makes it move (built in phase 3)
A source signal (the built-in **Flux LFO**: Rate, Shape Sine/Triangle/Ramp/Random) enters the field at one end and travels through it. Each cell reads it from a control-rate delay line at its own position on the path (`FluxPropagator`, also used for every LFO with Flux on).
- **Flux** (0..1, the big centre knob): how strongly the wave moves the field. It scales the cutoff movement (up to ±2 octaves) and the swell/duck of each cell's loudness (up to 60 %) together.
- **Travel** (0–4 s): time for the wave to cross the field. 0 = all cells move together.
- **Direction:** Up, Down, Outward (from the center), Inward (towards the center).
- **Viscosity:** lag that grows along the path, so the wave smears and slows as it travels.
- **Wavelength:** spatial cycles across the field. 0 = all cells move the same way; 0.5 = the two ends move in opposition; higher = cells breathe towards/away from each other.
- **Turbulence:** every cell gets its own slowly changing arrival time and depth, so the wave never moves in lockstep.
- **Flux Stereo:** the right channel hears the wave up to 250 ms later.

## 3. Organic / deep character (built in phase 2)
- **Character:** saturation inside each cell's resonance, set in output units so it behaves the same at any Q.
  - *Clean*: bends only at very high levels.
  - *Warm*: bends around 0 dBFS, the negative half earlier → even harmonics.
  - *Broken*: bends early and hard, cutoffs snap to a quarter-octave grid and update in steps (every 256 samples).
- **Feedback + Damping:** the wet signal goes back into the field through a lowpass and a soft clipper. Near the top the field self-oscillates into tuned drones (loop gain 1.35 × Feedback, since the field peaks just under unity).
- **Auto Gain:** protection only: turns the wet signal down (by up to 30 dB) when resonance, drive or feedback make it louder than 3 dB under the input; it never turns up, so a filter that removes part of the spectrum sounds quieter, as it should. ~300 ms. It holds its value while the input is silent, so tails and drones are not pumped up.
- **Safety:** DC blocker on the wet path, a soft +12 dBFS wet ceiling that is always on, and a switchable soft limiter at 0 dBFS (on by default).

## 4. Modulation system (phase 4, reworked in phase 5b)
**Sources:**
| Source | Details |
|---|---|
| LFO 1–3 | Analog style: shape buttons Sine, Triangle, Saw (rising), Square, Random (no drawing); Rate knob in Hz, or with Sync on the same knob steps through note divisions (4 bars … 1/32, dotted and triplets, sorted by length; locked to the host position while playing). Bipolar (±) or unipolar (+). **Flux** amount knob per LFO, styled like the big Flux knob (orange glow) |

**Connections:** drag an LFO's move icon onto a knob. 16 connection slots under the hood (Source, Target, Depth −1..1), stored as automatable parameters; a new connection starts at +25 %.
- Depth is in the target's knob range: depth 0.2 on Center moves the cells as far as turning Center by 20 %.
- **Per-cell targets:** Center (cell cutoffs), Resonance, Morph. An LFO's **Flux** amount (0..1) splits its movement: that share enters the field and travels through it like the flux wave (same Travel, Direction, Viscosity, Wavelength, Turbulence, Stereo), the rest hits all cells at once. The travelling shares per target are summed and share one propagator.
- **Global targets:** Spread, Width, Feedback, Damping, Flux and its controls, Drive, Mix, Gain, LFO 1–3 rate.
- Everything runs at control rate (every 32 samples). Modulation is added after parameter smoothing, so fast LFOs are not smeared.
- Ranges of all continuous parameters live in `params/ParamSpec.h`, shared by the host parameters and the modulation.
- Removed in phase 5b: envelope follower, macros, chaos, drift, the per-cell Wobble (organic drift), drawable LFO shapes, LFO phase, curve and via per connection.

## 5. Parameters
Cells: Density (1–16, default 8), Center, Spread, Resonance, Morph, Width (cells are spaced logarithmically and form a flat parallel bank; scales, tilt and serial blend remain in the DSP but have no parameter)
Character: Character (3-step knob: Clean, Warm, Broken), Drive, Feedback, Damping
Flux: Flux (amount), Rate, Shape, Travel, Direction, Viscosity, Wavelength, Turbulence, Stereo
Output: Mix, Gain (ID `output`), Auto Gain, Limiter
Mod: LFO 1–3 (rate, sync, division, shape, unipolar, flux), 16 connections × (source, target, depth) — 89 parameters in total

## 5b. UI (phase 5, reworked as one panel in phase 5b)
- **Header** as in audible_spec: 14 px margin, 30 px high, "flux_filter" right-aligned in Avenir Next Demi Bold 25 in the flux colour; version small on the left.
- **One panel**, laid out at 1140 × 794 and scaled as a whole when the window is resized (75–150 %, fixed aspect ratio).
- **Field display:** every cell as a resonance peak at its current frequency (width from Q, height from level), left channel sage #8FC9A9, right clay rose #E08A78, on a deep warm brown-black #1B1816; peaks reach about two thirds of the height at unity level; peaks reach zero at their edges and fill with a gradient that fades into the background. Behind them the **output spectrum** (after the filter): FFT 4096, 1/6-octave smoothing, +3 dB/oct tilt so typical music reads flat, fast rise / slow fall, auto range (the loudest band sits near the top, 54 dB span; the range rises quickly and falls back slowly), soft warm-grey (#D8CEC2) gradient fill with a glowing outline.
- **Top right, beside the display: Output** (as wide as Character): L/R meters on the left; Mix and Gain at full knob size with AUTO and LIMIT on the right; the meters have a clip LED that lights when the signal goes over 0 dBFS before the limiter (held 1.5 s, click to clear).
- **Middle row:** Cells (6 knobs, as wide as one LFO module) | **Flux**, the widest section: the big orange knob always sits at the horizontal centre of the window, Turbulence, Stereo, Viscosity, Wavelength in a 2 × 2 grid to its left; Rate and Travel to its right with the Shape and Direction icon buttons (same size as the LFO shape buttons) under them; all knobs normal size | Character (2 × 2 knobs, 10 px from the outer knobs to the border, like Cells).
- **Bottom row:** LFO 1–3 and the **Modulation** panel, all the same width. LFOs are blue, red, green (the Rate knob carries the colour), each with a borderless four-arrow move icon to drag onto knobs (click it to highlight everything that LFO modulates), the Rate knob with SYNC and a neutral ± button under it, and a Flux knob that looks like a small version of the big one.
- **Colours:** plugin background warm near-black #131110; field display as above; every other section is "warm charcoal" #2B2724 with recessed knob tracks and buttons in #1A1D22, borders #3B3531, text #EEE8E2 / #B0A79F. Flux orange #FFA23A is fixed; no section gets an orange border.
- **Knobs:** every knob is a round body (top-lit gradient #4A423C → #1F1B18, soft shadow, rim highlight, pointer line) inside a thick value arc; the flux knobs add an orange glow.
- **Selected knob:** no visual marker; the Modulation panel names it.
- **On the knobs:** each connection is a ring in the LFO's colour: from value − depth to value + depth for a bipolar LFO, from value to value + depth for a unipolar one; a white dot shows the live modulated position. Clicking a knob selects it.
- **Modulation panel:** lists the selected knob's connections with a bipolar depth bar (double-click resets to 0), a FLUX tag when the LFO's Flux amount is above 0, and a remove button.
- Values show in their units (Hz/kHz, ms, dB, %, oct, s) and accept typed input such as `2.5k`, `50 %`, `-3 dB`.
- Live data reaches the UI lock-free: atomics for cell frequency/Q/level, mod source values and meter peaks; a FIFO for the input samples.
- Dev aid (debug builds only): run the Standalone with `FLUXFILTER_UI_SNAPSHOT=<dir>` to render the panel to `ui_panel.png` (`FLUXFILTER_UI_DEMO=1` adds a few demo connections first).

## 5c. Presets (built in phase 6)
- **Factory presets** (in code, only the values that differ from the defaults): Init, Breathing Vowels, Slow Tide, Glass Rain, Deep Body, Broken Radio, Singing Drone, Ripple Sweep, Notch Phaser, Tempo Pulse. They are also the host's programs (AU factory presets, VST3 programs).
- **User presets:** the full state as XML in `~/Library/Audio/Presets/Tilo Hauk/flux_filter/<name>.fluxpreset`.
- **Preset bar** in the header (left): previous / list (Factory and User sections) / next, and SAVE (asks for a name; the same name overwrites). The preset name is stored with the project.
- Debug builds check that every factory preset only names existing parameters and in-range values.

## 6. Code structure
```
src/
  PluginProcessor.*        // APVTS, process loop, state save/load
  PluginEditor.*           // the single panel and its layout / scaling
  presets/PresetManager.h  // factory presets, user preset files, host programs
  ui/                      // look and feel, knobs with mod rings, field + spectrum display, flux/output sections,
                           // LFO modules, modulation panel, preset bar, ModContext (drag and drop, connections)
  dsp/FluxCell.h           // two-stage nonlinear ZDF SVF with morph
  dsp/FluxField.h          // cell array (max 16), placement, feedback, auto gain, safety
  dsp/FluxPropagator.h     // per-cell mod delay + viscosity smoothing + turbulence
  mod/ModSources.h         // LFO
  mod/ModMatrix.h          // connection slots, routing, flux propagation, control-rate evaluation
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
5b. **UI and workflow rework:** one panel, big Flux knob, drag-and-drop modulation with rings and a depth panel, analog LFOs with a Flux amount and polarity each, output spectrum, output meters with clip LEDs beside the display; envelope, macros, chaos, drift, wobble, scale and curve editor removed.
6. **Polish:** presets (factory + user, preset bar, host programs), CPU check (no optimisation needed, see below), pluginval at strictness 10 on VST3 and AU.

## Verification
- Build in CLion / `cmake --build cmake-build-debug`, run Standalone with test audio (noise, drums, pads).
- Check stability: max resonance + feedback + fast modulation, no NaN/blowups (add debug assertion on output).
- CPU: `FluxBenchmark` (tests/Benchmark.cpp, -O2), stereo, block 256, % of one core (Apple Silicon, two-stage cells):
  | | 8 cells | 16 cells | 16 cells + 16 connections |
  |---|---|---|---|
  | 48 kHz | 0.9 % | 1.4 % | 1.6 % |
  | 96 kHz | 1.4 % | 2.6 % | 3.0 % |
- pluginval on VST3 + AU: passes at strictness 10 (phase 6). The AU shows only the warning "Current program is -1" (no preset selected yet), which is harmless. `auval` passes. Still to do by hand: test in Ableton Live 12.
- Switch parameters are snapped to exactly 0/1 after a state restore (pluginval sets in-between values that JUCE's replaceState would otherwise skip).
- Automation + state recall (save/reload project, mod matrix restored).

## Open later (not v1)
Step sequencer / MIDI keytrack to play the field, custom animated UI, spectral "freeze" of the field, sidechain input as env source.

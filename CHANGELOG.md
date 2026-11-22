# Changelog

All notable changes to flux_filter. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [semantic versioning](https://semver.org/).

## [1.0.0] - 2026-11-22

The first public release.

### Sound
- Formant field engine: up to 16 resonant cells grouped into up to five formants, with nine throat
  shapes (a, e, i, o, u, nasal, growl, schwa, whisper) and the halfway points between them (Mode 1–18).
- Shape controls: Density, Mode, Shift (±2 octaves), Stretch (formant spacing), Resonance, Scatter.
- Living cells: chaotic drift, energy coupling, birth and death, driven by one **Flux** macro from a
  still formant filter to an unstable organism; Wander and Rasp refine it.
- Character section: Clean, Warm or Broken saturation, Drive, and a damped feedback loop.
- Output: Mix (50 % by default), Gain, automatic gain compensation and a limiter. Mono in stays
  mono out; the filter does not change the stereo width of the input.

### Modulation
- Two LFOs (sine, triangle, saw, square, random), free or tempo-synced, bipolar or unipolar.
- Drag-and-drop connections onto any knob, shown as coloured dots; the value arcs follow the
  modulation. Connections have a depth and an on/off switch, and are automatable.
- Each LFO's Flux control hands its movement to the living cells.

### Interface
- Single-panel UI with a live response display whose colour follows Flux, and an animated field
  around the industrial-style Flux knob.
- 14 factory presets; user presets can be saved.

### Platforms
- VST3 and standalone on macOS, Windows and Linux; AU on macOS (universal: Apple silicon and Intel);
  LV2 on Linux.
- Builds for all three systems are produced by GitHub Actions.
- Released under the GNU Affero General Public License v3.0.

[1.0.0]: https://github.com/tilohk/flux_filter/releases/tag/v1.0.0

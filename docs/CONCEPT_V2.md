# FluxFilter v2 — A Field That Lives

## Why v2
v1 is a bank of evenly spaced (log) resonant cells moved by one travelling LFO wave. That is, technically, a many-stage phaser: a regular comb of peaks and notches that glides up and down. The two things that make it sound like one:
- **Hollow comb tone:** evenly spaced, equally narrow peaks.
- **Sweeping motion:** one smooth, periodic wave moves everything.

v2 keeps the visual concept, the single panel, the big Flux knob and the drag-and-drop LFOs. It replaces the sound engine with a **formant field** that **lives**: its cells drift chaotically, pass energy to each other, and are born and die.

## Direction (from the planning Q&A, 2026-11-21)
| Topic | Decision |
|---|---|
| Sound goal | Throats & vowels + living instability |
| Movement | Cells interact (energy coupling, life cycle) + autonomous chaos; not mainly LFO sweeps |
| Material | Generic / experimental: must work on anything (drums, pads, noise, voice) |
| Cell layout | Formant groups gliding through a vowel/throat space |
| Life cycle driver | Input energy and chance, blendable |
| Grit | Rasp/growl as an amount (0 = clean); saturation stays in Character |
| Tuning | Free by default, optional pitch anchor |
| Flux knob | Redefined: "how alive the field is" |
| Flux section | The space of the old wave controls hosts the new life controls |
| LFOs | Unchanged (3 analog LFOs, drag and drop onto any knob, including the new ones) |

## The idea in one sentence
FluxFilter v2 is a **throat made of living cells**: a few broad formants that shape any sound into vowels and growls, whose cells wander, feed on the input, ring into each other, and are born and die. **Flux** sets how alive it is, from a still formant filter to an unstable organism.

## 1. Signal flow
```
in → Input Drive → [ FORMANT FIELD: up to 16 cells in 1–5 formant groups ]
                      ↑ shaped by: Vowel · Size · Resonance · Scatter · Width
                      ↑ moved by:  LIFE (chaos drift, energy coupling, life cycle, rasp) × Flux
                      ↑ optional:  Pitch anchor
   → Feedback loop (Damping + soft saturation) → Mix → Output (Auto Gain, Limiter)
```
Kept from v1 unchanged: `FluxCell` (two-stage nonlinear ZDF SVF), Character (Clean/Warm/Broken), Drive, Feedback/Damping, field level normalisation (the field never amplifies a tone), Auto Gain, DC blocker, wet ceiling, limiter, control rate of 32 samples.

## 2. Formant field (replaces the even log bank)
- **Formant groups:** the active cells are divided among 1–5 formants (F1…F5). Each formant is made of 2–4 cells, slightly detuned around its centre (a few cents up to ~1/6 octave, randomised per cell, re-rolled at birth). A formant is therefore a broad, uneven hump rather than one sharp spike, which removes the comb.
- **Realistic bandwidths:** formant bandwidths come from the vowel table (wide for F1/F2, wider still for higher formants). **Resonance** scales all bandwidths together, from wide and breathy to narrow and singing.
- **Vowel table:** nine throat shapes, each a list of formant frequencies, bandwidths and relative levels: *a, e, i, o, u* plus four non-speech shapes: *nasal* ("ng/m": low F1, muffled, humming), *growl* (low, close F1/F2, thick), *schwa/open* (neutral relaxed throat, evenly spread formants), *whisper/hiss* (only high formants, F1 almost gone, airy). Stored in a new `dsp/FormantTable.h`.
- **Vowel** (replaces Center): position along the ring of shapes (a → e → i → o → u → nasal → growl → schwa → whisper → back to a), interpolated in log frequency. The order is a first guess, to be tuned by ear in phase 1 so neighbouring shapes morph smoothly. Internally the field has a 2-D throat position (vowel × openness); the knob sets the base and chaos moves around it in 2-D, so movement leaves the ring and is not a straight sweep.
- **Size** (replaces Spread): throat length. It scales all formant frequencies together, ±2 octaves (tiny bird throat ↔ giant cave). Formant ratios are kept, so a vowel stays recognisable as it grows.
- **Scatter** (replaces Morph): 0 = cells sit on their formants; 1 = cells leave the formant structure and sit at irregular, individually drifting positions with uneven widths (the "free cells" mode). In between, cells are pulled towards formants with a weakening spring. All cells are bandpass by default; the LP/HP/notch morph is removed as a parameter (the DSP keeps it).
- **Density** keeps its range (1–16 cells). The number of formants follows from it (1–3 cells: 1 formant; 4–6: 2; 7–9: 3; 10–12: 4; 13–16: 5); there is no separate Formants parameter.
- **Width:** as in v1 (L/R detune and alternating pan), plus the Stereo life mode (section 3).

## 3. Life (Flux redefined)
Each cell carries a small state at control rate:
```
energy    – smoothed output level of the cell (RMS, ~20 ms)
vitality  – 0..1, its life (scales its gain; birth/death)
offset    – its own drift around its formant position (octaves)
age       – time since birth
```

### 3.1 Flux: the master "aliveness"
**Flux** (0..1, the big centre knob) scales every life process together:
- Flux 0: all cells fully alive, no drift, no coupling, no rasp. A **still formant filter** (Vowel, Size, Resonance fully define the sound). Useful as the first preset and as the reference for the "not a phaser" listening test.
- Rising Flux: more chaotic drift range, stronger coupling, deeper life cycle (cells die further and more often), more rasp.
The six detail knobs set *how* the field lives, Flux sets *how much*.

### 3.2 Chaotic drift
- An autonomous chaos source (slowed Lorenz-style attractor or several coupled random walks with mean reversion, `dsp/FluxChaos.h`) moves the throat position (vowel × openness), Size by a little, and each formant's offset with its own decorrelated component.
- Never loops; smooth (no steps) at any setting.
- **Drift** (replaces Rate): speed of the chaos, ~0.01–5 Hz equivalent.
- **Wander** (replaces Travel): how far it roams (throat position range, per-formant offset up to ±1 octave), multiplied by Flux.

### 3.3 Energy coupling
- A ringing cell passes energy to its **neighbours in frequency** (within the same formant first, then adjacent formants), like sympathetic strings.
- Two layers:
  1. *Audio-rate bleed:* a fraction of each cell's output is added to its neighbours' input (through a soft clipper, total loop gain bounded so the field stays below self-oscillation unless Feedback pushes it).
  2. *Control-rate transfer:* a cell's energy raises its neighbours' vitality and narrows their bandwidth a little, so resonance visibly and audibly **wanders across the field**.
- **Coupling** (replaces Viscosity): amount of both layers, multiplied by Flux.

### 3.4 Life cycle
- A cell is born (vitality rises with a soft attack), lives, then fades and dies; a dead cell is silent and is reborn later at a new position (near a formant with fresh detune, or anywhere when Scatter is high).
- **Feed** (replaces Wavelength), blend of two drivers:
  - *Energy (Feed → 1):* cells live off the input energy that falls into their band. Fed cells grow and stay, starved cells fade; silence lets the population die out (tails ring out naturally, no pumping). Strong input in an empty region gives birth to a cell there.
  - *Chance (Feed → 0):* a self-running population: each cell has a random lifespan around **Lifespan**, births happen at random times.
- **Lifespan** (replaces Turbulence): mean life of a cell, ~50 ms (crackling, granular flicker) to ~20 s (slow breathing).
- Depth of the cycle (how far vitality drops) is scaled by Flux; at Flux 0 every cell lives forever.
- The field's level normalisation runs on the living cells, so the total level stays even while the population changes.

### 3.5 Rasp
- **Rasp** (replaces Stereo): throat roughness at audio rate.
  - Jitter: filtered noise (~30–300 Hz band) on each cell's frequency and amplitude, decorrelated per cell.
  - Growl / vocal fry: subharmonic period doubling. Alternate cycles of the cell's output are attenuated (cycle detected by zero crossings), which adds a sub-octave roughness.
- 0 = clean. Saturation colour stays with Character.

### 3.6 Icon buttons in the Flux section
Replacing Shape and Direction:
- **Pitch anchor:** Off / Scale / Follow.
  - *Scale:* cell centres are pulled towards the nearest note of a key and scale (key and scale as small selectors; scales: chromatic, major, minor, pentatonic, harmonic series of the key root).
  - *Follow:* pulled towards the harmonics of the input's detected pitch (YIN-style detector at control rate, holds the last pitch in unpitched passages).
  - Pull strength is fixed (cells snap to the notes); there is no Anchor knob. How tonal it sounds follows from Resonance: narrow bandwidths sing, wide ones stay subtle.
- **Stereo life:** Linked (both channels share one population and one chaos source; Width only detunes) / Independent (each channel has its own population, chaos and rasp: wide, unstable stereo).

### 3.7 LFOs
LFO 1–3 stay as they are: shapes, sync, polarity, drag and drop, 16 connection slots, mod rings. All new knobs are mod targets. Targets that no longer exist are removed (Rate, Travel, Viscosity, Wavelength, Turbulence, Stereo of the wave; Center, Spread, Morph become Vowel, Size, Scatter).

Each LFO's small **Flux** knob gets a new meaning, since the travelling wave is gone: the share of that LFO which is **absorbed by the life** of the field. Each living cell receives it with its own small random lag and depth (reusing `FluxPropagator`'s per-cell delay, viscosity and turbulence internally, with no wave travel), weighted by the cell's vitality. 0 = all cells move together exactly as the LFO says.

## 4. Parameters (v2)
| Section | Parameters |
|---|---|
| Cells | Density, Vowel, Size, Resonance, Scatter, Width |
| Flux (life) | **Flux**, Drift, Wander, Coupling, Feed, Lifespan, Rasp; buttons Pitch anchor (+ key, scale), Stereo life |
| Character | Character, Drive, Feedback, Damping |
| Output | Mix, Gain, Auto Gain, Limiter |
| Mod | LFO 1–3 (rate, sync, division, shape, unipolar, flux), 16 connections × (source, target, depth) |

Removed: Flux Rate, Shape, Travel, Direction, Viscosity, Wavelength, Turbulence, Stereo; Center, Spread, Morph.
State and presets from v1 are not migrated (the plugin is unreleased): v1 projects and user presets load as defaults.

## 5. UI changes
- Layout stays as it is. In the Flux section the 2 × 2 grid becomes Coupling, Feed, Lifespan, Rasp; Drift and Wander sit to the right of the big knob with the Pitch anchor and Stereo life icon buttons under them.
- Field display: each cell's peak height and alpha follow its vitality, so births and deaths are visible; cells of one formant share a slightly tinted fill so the formant groups read as humps; a faint marker for each formant target position. Energy flowing between cells can show as a brief glow on the receiving peak.
- Cells section labels: Density, Vowel (shows the nearest shape name, e.g. "a → e"), Size, Resonance, Scatter, Width.

## 6. Code structure
```
src/dsp/FluxCell.h        // unchanged (two-stage nonlinear ZDF SVF)
src/dsp/FormantTable.h    // NEW: throat shapes, interpolation on the vowel ring, Size scaling
src/dsp/FluxChaos.h       // NEW: smooth chaotic multi-output source (throat position, per-formant offsets)
src/dsp/FluxLife.h        // NEW: per-cell energy, vitality, birth/death, coupling transfer
src/dsp/FluxRasp.h        // NEW: audio-rate jitter + subharmonic period doubling
src/dsp/PitchAnchor.h     // NEW: scale quantiser + input pitch detector
src/dsp/FluxField.h       // reworked: formant placement, audio-rate bleed, life integration; feedback/safety kept
src/dsp/FluxAbsorber.h    // NEW (phase 7): the LFO flux share, absorbed by the living cells (replaces FluxPropagator.h)
src/params/*              // new IDs and ranges; removed wave parameters
src/ui/*                  // Flux section and Cells labels, field display vitality/formant drawing
src/presets/PresetManager.h // new factory set
```

## 7. Build phases
1. **Formant field:** FormantTable, formant grouping of cells with detune, Vowel / Size / Resonance / Scatter; remove the wave from the signal path. Flux = 0 behaviour only. *Listen: must sound like a throat, not a comb.*
   **Built (2026-11-21):**
   - `dsp/FormantTable.h` (nine shapes, ring interpolation, formant count per Density); `FluxField` places cells by formant; parameters `vowel` (0–9, shows the shape name, e.g. "a/e"), `size` (±2 oct, shown as a factor "x2.00"), `scatter`; mod targets Vowel / Resonance / Scatter (per cell) and Size.
   - Resonance scales the table bandwidths: 60 % = the table, 0 % = 12× wider, 100 % ≈ 5× narrower.
   - A formant's cells spread over half its bandwidth (max 1/6 oct) plus an own offset of 15 % of that; Width detunes L/R by at most a third of the formant's width (0.1 oct for scattered cells). Both scale with bandwidth: spread wider than their own bandwidth, detuned cells partly cancel each other, which also kept feedback from self-oscillating.
   - Scattered cells use fixed per-cell traits (position 100 Hz–6 kHz, Q, level), seeded by cell index, so presets recall exactly.
   - Removed: the built-in flux LFO (`FluxLfo.h`), the travelling wave in the field, the serial chain, cell scales and tilt. The Flux section's knobs (Flux, Rate, Shape, Travel, Direction, Viscosity, Wavelength, Turbulence, Stereo) currently do nothing in the field; phases 2–7 give them their new meaning. An LFO's Flux share still travels through the cells (Travel etc. apply to it).
   - Factory presets moved to Vowel / Size / Scatter ("Notch Phaser" became "Vowel Glide"); a full new set follows in phase 8.
   - CPU (stereo, block 256, 48 kHz): 8 cells 0.9 %, 16 cells 1.4 %, 16 cells + 16 connections 1.9 % of one core.
   - **Makeup:** narrow formants pass little of a broadband sound (pink noise came out ~19 dB under the input). The field now estimates, every 16 control ticks, how much pink noise (equal energy per octave, 20 Hz–20 kHz) its current layout passes, using the complex sum of all cells, since neighbouring formants partly cancel between their peaks. It then lifts the wet signal to 3 dB under the input, by at most +12 dB. The cap means a tone sitting on a formant peak comes out at most 12 dB louder than it went in; Auto Gain (when on) and the limiter catch the rest. Makeup is always on, glides with the other settings, and sits after the feedback loop (loop gain is unchanged). Pink noise: Resonance 30 % −5 dB, 60 % −6 dB, 90 % −12 dB (cap reached).
2. **Chaos drift:** FluxChaos, Drift / Wander, Flux as master. *Listen: movement never repeats, no sweep.*
   **Built (2026-11-21):**
   - `dsp/FluxChaos.h`: the throat position comes from a slowed Lorenz attractor. x moves the vowel, z sets openness (the jaw: F1 ±0.4 oct, other formants ±0.12 oct). It circles one lobe, then jumps to the other at unpredictable moments, so the throat wanders between two vowel neighbourhoods rather than sweeping. Size and each formant's own offset come from smooth value noise (random targets, cosine-interpolated, two layers at unrelated rates), each seeded on its own.
   - How far it roams = Flux × Wander: vowel ±2 shapes, size ±0.25 oct, each formant ±1 oct at full. Flux 0 or Wander 0 is a still formant filter. Defaults (Flux 25 %, Wander 50 %, Drift 0.2 Hz) give a gentle breathing.
   - **Drift** (0.01–5 Hz, replaces Flux Rate, ID `drift`) and **Wander** (0–1, replaces Travel, ID `wander`), both mod targets, sit where Rate and Travel were. Travel for an LFO's Flux share is fixed at 0.8 s until phase 7.
   - Deterministic: the chaos starts from the same state on every prepare, so a preset moves the same way every time playback starts.
   - Tests: still at Flux 0 / Wander 0; at full the F1 and F3 cells roam ~2 oct, independently (correlation 0.3), at most 0.002 oct per control tick; Wander 0.25 roams less than half; Drift 4 Hz travels ~7× the path of 0.5 Hz.
   - CPU (stereo, block 256, 48 kHz): 16 cells + 16 connections 2.0 % of one core.
3. **Energy & coupling:** per-cell energy, audio-rate bleed with bounded loop gain, control-rate transfer.
   **Built (2026-11-21):**
   - **Coupling** (0–1, replaces Viscosity, ID `coupling`, default 30 %, mod target); strength = Flux × Coupling.
   - Neighbours are the cells next to each other in the field: weight 1 inside a formant, 0.5 across a formant border.
   - *Audio-rate bleed:* each cell also hears its neighbours' output from the previous sample, times up to 0.25. With at most weight 1 per side the loop gain stays below 0.5, so it never rings on by itself (no clipper needed). It makes neighbouring cells ring together like sympathetic strings.
   - *Control-rate transfer:* each cell's energy (output level, 20 ms) relative to the field's average. A cell whose neighbours ring harder than average gets excited: up to +6 dB louder and 1.5× narrower, rising in ~80 ms, letting go over ~1.5 s. Resonance spreads outward from where the input hits and wanders on. Tone on the top F1 cell (9 cells, full coupling): excitation 1.0 / 0.97 inside F1, 0.24 across into F2, 0 in F3.
   - Level: the excited gains go through the field's normalisation; measured, tones and noise stay within ±1.5 dB of no coupling, so no extra compensation.
   - The excitation is the first per-cell life state; phase 4 builds vitality on it.
   - The Viscosity of an LFO's Flux share is fixed at 0.2 until phase 7.
   - CPU (stereo, block 256, 48 kHz): 16 cells + 16 connections 2.3 % of one core (from 2.0 %).
4. **Life cycle:** vitality, birth/death, Feed and Lifespan, normalisation over living cells.
   **Built (2026-11-21):**
   - **Feed** (0–1, replaces Wavelength, ID `feed`, default 50 %) and **Lifespan** (0.05–20 s, replaces Turbulence, ID `lifespan`, default 2 s), both mod targets.
   - Each cell is alive (vitality rises, attack 0.15× Lifespan) or dead (vitality falls, release 0.3× Lifespan); a *hunger* value decides when it dies. Feed blends the two rates:
     - *Chance:* hunger grows by itself; the cell dies after its own lifespan (0.5–1.5× Lifespan) and is reborn after a rest of 0.15–0.55× Lifespan.
     - *Energy:* a cell eats the input energy in its band. Fed cells (own energy well above −70 dB and a quarter of the field's average, or excited by their neighbours) lose hunger; starved ones die after about Lifespan. A dead cell keeps listening at its next place and is reborn as soon as there is food there (≥ 50 ms). Silence starves the population.
   - Rebirth: once silent, a dead cell moves. Inside its formant it moves by up to the formant's whole span; when scattered it can land anywhere. The move is scaled by Flux.
   - Depth: a cell's gain is 1 − Flux × (1 − vitality). At Flux 0 every cell lives forever and nothing moves.
   - Level: the field normalises over the living cells, lifting a thinned population by at most +12 dB over the full field, so a dying population still fades out. The makeup estimate counts only living cells. Measured on pink noise in chance mode: level equal to Flux 0, per-second swing under 1.5 dB (it was 13 dB before living-cell normalisation).
   - Measured: Lifespan 0.2 s gives ~240 births in 10 s (12 cells), 2 s gives ~22. A tone on F1 in energy mode keeps the F1 cells alive while F2/F3 die; 3 s of silence kills all; the tone brings them back.
   - Deterministic: the life random generator is reseeded on every prepare.
   - CPU (stereo, block 256, 48 kHz): 16 cells + 16 connections 2.3 % of one core.
5. **Rasp:** jitter and subharmonic growl.
   **Built (2026-11-21):**
   - **Rasp** (0–1, replaces Flux Stereo, ID `rasp`, default 0 = clean, mod target); strength = Flux × Rasp.
   - *Jitter:* every cell's frequency (up to ±0.08 oct) and level (up to ±50 %) shake with their own band-limited noise. The noise is value noise at ~180 Hz in two layers, per cell and channel, updated every control tick (1.5 kHz at 48 kHz); the level is ramped per sample. Measured at full: ~0.006 oct per tick, 0.15 oct in all.
   - *Growl:* every other cycle of each cell (counted at rising zero crossings) is turned down by up to 80 %. The period doubles and a sub-octave appears, like vocal fry. Both halves are scaled so the power stays the same. Measured on a 650 Hz tone: sub-octave −65 dB (clean) → −12 dB (Rasp 50 %) → −4 dB (100 %) relative to the fundamental.
   - Level on noise within ~1 dB of clean. Saturation colour stays with Character.
   - Presets: Broken Radio and Deep Body use Rasp.
   - CPU (stereo, block 256, 48 kHz, every life process on incl. Rasp 50 %): 16 cells + 16 connections 3.0 % of one core.
6. **Pitch anchor and Stereo life.**
   **Built (2026-11-21):**
   - `dsp/PitchAnchor.h`: scale quantiser and a YIN pitch detector.
   - **Pitch** icon row (replaces Shape, ID `anchor`): Off / Scale / Follow; with **Key** (C…B, ID `anchorKey`) beside it and **Scale** (Chromatic, Major, Minor, minor Pentatonic, Harmonic = harmonic series of the key's root from C1, ID `anchorScale`) under it.
   - Snapping comes after drift, life, width and modulation; Rasp's jitter still shakes the cells on top. Each cell glides to its new note in ~8 ms (no clicks when it hops). With anchoring on, Broken's quarter-octave grid is off. Pull strength is fixed, so Resonance decides how tonal it sounds.
   - *Follow:* mono input, decimated to ~12 kHz, analysed every ~20 ms over 50–1000 Hz (threshold 0.15, parabolic interpolation). Quiet (< −60 dBFS) or unpitched passages keep the last pitch. Cells snap to the nearest harmonic. The detector only runs in Follow mode. Measured: within 2 cents on sine and saw from 55 to 880 Hz; on a 110 Hz saw, 109.98 Hz.
   - Measured in Scale mode (A minor pentatonic): cells on a note (±5 cents) 100 % of the time when still, 98 % while drifting (Wander 50 %, Drift 0.2 Hz), 88 % at full Wander.
   - **Stereo** icon row (replaces Direction, ID `stereoLife`): Linked / Independent. Independent gives the right channel its own chaos, its own population and its own rasp noise. Both chaos sources always run, so switching is seamless. Coupling's energy stays shared. In mono the field runs linked.
   - Presets: Slow Tide (harmonics of A), Singing Drone (A minor pentatonic), Ripple Sweep (Follow), Glass Rain (independent).
   - The LFOs' Flux share now always travels Up (Direction is gone) until phase 7.
   - CPU (stereo, block 256, 48 kHz, everything on incl. independent stereo): 16 cells + 16 connections 3.0 %, with Follow 3.3 % of one core.
7. **Parameters, LFO flux redefinition, UI:** Flux section, Cells labels, field display.
   **Built (2026-11-21):**
   - **LFO Flux, absorbed by life:** `dsp/FluxAbsorber.h` replaces `FluxPropagator.h`. Each cell hears its share of an LFO with its own delay (0–300 ms), depth (0.6–1.4×) and sluggishness (up to 120 ms), times its life gain, so a dead cell absorbs nothing. At LFO Flux 0 every cell moves exactly with the LFO. The right channel has its own traits when Stereo life is independent. Measured (12 cells): lags spread over ~0.3 s, swing 0.38–0.84 for one connection.
   - All remnants of the travelling wave are gone (Travel, Direction, Viscosity, Wavelength, Turbulence, Flux Stereo settings); tooltips and comments now speak of absorbing.
   - **Field display:** the cells of each formant mix a slight tint (F1 sand, F2 amber, F3 mauve, F4 blue-grey, F5 moss) into the channel colour. A small "F1…F5" label and tick sits under each formant's current target (vowel, drift, openness, size), fading out as Scatter rises. A cell excited by its neighbours gets a soft orange halo. Peak height already follows vitality, so births and deaths show.
   - The Flux knob's tooltip describes it as "how alive the field is".
   - CPU (stereo, block 256, 48 kHz, everything on): 16 cells + 16 connections 2.8 %, with Follow 3.2 % of one core.
8. **Polish:** new factory presets, benchmark, pluginval (strictness 10, VST3 + AU), auval, Ableton Live 12 by hand.
   **Built (2026-11-21):**
   - New factory set (14 presets, each showing one side of the engine): Init; Breathing Throat (drift); Whisper Choir (whisper, independent stereo); Growling Beast (growl + rasp); Feeding Swarm (energy-fed life, for drums); Flicker Grain (short-lived scattered cells); Singing Stones (A minor pentatonic, feedback); Harmonic Shadow (Follow); Cave Mouth (huge, slow); Tiny Creature (tiny, nervous); Slow Tide (whole vowel ring, absorbed LFO); Broken Radio (Broken, nasal, synced square on Resonance); Vowel Sequencer (synced random LFO on Vowel); Drone Organism (self-oscillating on harmonics of D).
   - `tests/PresetCheck.cpp` (target `FluxPresetCheck`, built only on request because it compiles JUCE a second time) runs every factory preset through the real plugin (parameters → processor → field) on pink noise and a saw chord. It checks that the output is finite, at most 0 dBFS, not silent, and not more than 6 dB louder than the input. With per-preset Gain all 14 land between −4 and −8 dB under the input. Before that, Singing Stones and Cave Mouth were −12 dB.
   - CPU (`FluxBenchmark`, -O2, stereo, block 256, % of one core, Apple Silicon; every life process on incl. independent stereo, Rasp 50 %):
     | | 8 cells | 16 cells | 16 cells + 16 connections | + Follow |
     |---|---|---|---|---|
     | 48 kHz | 1.3 % | 2.3 % | 2.7 % | 3.1 % |
     | 96 kHz | 2.2 % | 4.3 % | 5.0 % | 5.4 % |
     The concept's target (16 cells stereo at 48 kHz under 3 %) holds, except with Follow (3.1 %).
   - pluginval at strictness 10 passes on VST3 and AU (debug build). The AU shows only the known, harmless warning "Current program is -1". `auval` passes.
   - Still to do by hand: Ableton Live 12 (playing, automation, state recall, CPU in a real set).

## Verification
- Listening set covering the generic/experimental brief: white and pink noise, drum loop, sustained pad, spoken voice, field recording.
- **Phaser test:** at Flux 0 with a static vowel, pink noise must sound like a vowel, not a comb; with Flux up, no audible periodic sweep at any Drift setting.
- Stability: max Coupling + max Feedback + max Resonance + Rasp + fast LFOs: no NaN, no runaway (debug assertion on output); coupling alone never self-oscillates.
- Life: silence lets the field die out without pumping; restarting input gives births again; preset recall reproduces the sound character (chaos is seeded, not identical).
- CPU target: 16 cells stereo at 48 kHz under 3 % of one core (v1: 1.6 % with 16 connections); pitch detector only runs in Follow mode.

## Decisions on the former open questions (2026-11-21)
1. Number of formants: derived from Density, no own parameter.
2. Pitch anchor strength: fixed; Resonance decides how tonal it sounds.
3. LFO Flux knob: "absorbed by the life" (section 3.7).
4. v1 state and presets: dropped, no migration.
5. Throat shapes: a, e, i, o, u + nasal, growl, schwa/open, whisper/hiss; ring order tuned by ear in phase 1.

## Rework after the first listening (2026-11-21)
Feedback after trying v2: the big Flux knob acted like a mix for the Flux section; the many small
knobs and the pitch/scale/stereo controls were unusual for a filter; the display showed separate
cells where the ear hears one filter. Changes:

- **Flux is a macro (calm → chaotic).** It feeds the field with energy and movement and drives every
  life process on its own curve (`FluxMacro` in `FluxField.h`):

  | Flux | 0 | 0.5 | 1 |
  |---|---|---|---|
  | Drift speed | 0.05 Hz | 0.35 Hz | 2.5 Hz |
  | Lifespan | 12 s | 1.7 s | 0.25 s |
  | Coupling | 0 | 0.35 | 1 |
  | Feed (1 = input energy, 0 = chance) | 0.8 | 0.55 | 0.3 |
  | Life depth | 0 | 0.5 | 1 |

  Drift, Lifespan, Coupling and Feed are no longer parameters. **Wander** and **Rasp** stay as the
  only secondary Flux knobs (both still scaled by Flux, so Flux 0 is always a still, clean filter).
- **LFO Flux share:** at field Flux 0 the living cells follow the LFO in lockstep; as Flux rises they
  spread out with their own delay, depth and sluggishness (`FluxAbsorber::process (…, scatter)`).
- **Size → Shift**, in octaves, + = higher formants.
- **Dropped:** pitch anchor (Scale/Follow, key, scale; `PitchAnchor.h` deleted), stereo life, and
  the Width knob. Both channels now share one chaos, one population, one rasp and identical
  coefficients, so the stereo image passes through unchanged (a mono signal stays bit-identical L/R).
- **Display:** one line, the combined response (all living cells summed with their phases, the
  feedback loop with damping, makeup/auto gain and the dry/wet mix), on a dB scale with a 0 dB
  reference. Excited cells make the line glow in the flux colour. No per-cell peaks, formant
  markers or cell count.
- **UI:** the Flux section holds the big knob with Wander and Rasp beside it; the Cells section has
  five knobs; the window is 980 px wide (was 1140).
- Presets reworked to the new parameters ("Harmonic Shadow" became "Vowel Shadow").
- Verified: FluxFieldTest and FluxModTest pass (new: mono stays mono, Flux macro curves and births,
  lockstep at Flux 0); FluxPresetCheck passes; pluginval strictness 10 (VST3, AU) and auval pass.
  CPU not re-measured (the pitch detector and the second population are gone, so it can only drop).

### Details round (2026-11-21)
- **Mix** defaults to 50 %; no factory preset overrides it any more. Preset output gains were
  lowered to match (all presets now land 3.4–5.3 dB under the input on noise and saw).
- **Cells → Shape** (section and parameter group). New sixth knob **Stretch** (−1..1, modulatable):
  the spacing of the formants above F1 in octaves × 0.62…1.62, bandwidths scaled along, so familiar
  vowels turn strange.
- **LFO 3 dropped** (source, rate/flux parameters, rate target); "Drone Organism" uses LFO 2 instead.
- **Layout:** bottom row LFO 1 | LFO 2 | Modulation (equal widths); middle row Shape (as wide as an
  LFO, 3 × 2 knobs) | Flux (rest: the big knob at the window's horizontal centre, Wander and Rasp stacked to its right) |
  Character (as wide as Output). Window 880 × 778 (was 980 × 794).
- **Display:** the response line is computed at 400 points and averaged over about 1/5 octave
  before drawing (quadratic midpoint path), so narrow resonances read as soft hills, not needles.
- **Modulation panel:** each connection is two lines (source and value, then a thick bar in the
  source's colour across the whole row); the FLUX badge is gone.
- **Flux aura:** an animated magnetic field around the big knob (`FluxAura` in `Sections.h`, an
  overlay over the whole editor panel). Five poles sit evenly along the value ring (its ends at 225°
  and 135°, ±67.5°, and the top); nested field lines arc between neighbouring poles
  (broad arcs, r = R + (L − R) · sin (πt)^0.35, so they rise quickly and stay high) and light pulses flow along them upwards: from the bottom poles
  via the middle ones into the top one. The field stays close to the knob (outermost line 1.16–1.22 ×
  the ring radius); Flux adds lines (3 → 6), brightness, glow, pulse speed and sway.
- **Big Flux knob look** (`drawHeroBody` in `FluxLookAndFeel.h`): an analog, industrial control,
  kept clean, in the theme's warm greys and amber: a dark grip ring with fine low-contrast
  knurling, a smooth top-lit dark-steel cap with a thin machined edge, an engraved pointer and a
  small amber lamp at its tip that glows with the amount. The other knobs keep their plain look.
- **Flux modulation is visible:** the big knob's value arc, its lamp and the magnetic field follow
  the modulated value (knob + live modulation offset, from the UI's modulation readout); the pointer
  stays on the knob's own setting.

### UI round (2026-11-22)
- **Field animation follows Flux smoothly:** every moving part of the aura integrates its own speed
  (no more `time × speed`), so turning or modulating Flux changes the speed from that moment on
  instead of making the animation race while the value changes.
- **Vowel → Mode** (parameter id `mode`): shown as numbers 1–18, one for every throat shape and one
  for every halfway point between two (mode 1 = shape 1, mode 2 = halfway to shape 2, …; the ring
  wraps). Typed input takes the number.
- **Output section in white:** Mix/Gain value arcs and the switched-on AUTO/LIMIT pills.
- **LFO modules:** sync (a metronome icon) and polarity are equal-sized buttons stacked between the
  Rate and Flux knobs; the bottom row is 22 px lower, the window 880 × 756. The modulation panel
  fills the same row (three connections visible).
- **Modulation on knobs:** the value arc of every modulated knob moves with its modulation (the
  pointer stays on the setting); a small dot per source, in its colour, sits at the bottom of the
  knob. The range arcs and the white live dot are gone. The modulation panel is laid out for two
  connections (one per LFO), with taller rows.
- **LFO modules:** sync and polarity stacked on the left, then Rate, then Flux.
- **Connections can be switched off:** each slot has an `On` parameter (automatable, default on).
  The source dot in the modulation panel is the switch (filled = on, ring = off); an off connection
  keeps its depth but does nothing, and its dot under the knob turns into a ring. The panel splits
  its height into two halves, one per possible connection.
- **LFO colours:** dusty lavender (LFO 1) and clay rose (LFO 2), to sit with the warm theme.
- **Filter line colour follows Flux** (after modulation, published by the field): sage at 0, the
  flux amber at 100, line and fill. The coupling glow on the line is gone.

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
src/dsp/FluxPropagator.h  // kept internally for the LFO flux share (no wave travel)
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
6. **Pitch anchor and Stereo life.**
7. **Parameters, LFO flux redefinition, UI:** Flux section, Cells labels, field display.
8. **Polish:** new factory presets, benchmark, pluginval (strictness 10, VST3 + AU), auval, Ableton Live 12 by hand.

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

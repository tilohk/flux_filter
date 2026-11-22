#pragma once

#include "FluxCell.h"
#include "FluxChaos.h"
#include "FormantTable.h"
#include "mod/ModMatrix.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

namespace flux
{

enum class Character { Clean = 0, Warm, Broken };

// Everything that shapes the field. Plain values, filled from the plugin parameters each block.
struct FieldSettings
{
    int   density    = 8;       // number of active cells, 1..maxCells; also sets the number of formants
    float vowel      = 0.0f;    // ring position 0..9 through the throat shapes (dsp/FormantTable.h)
    float shiftOct   = 0.0f;    // moves every formant, -2..+2 octaves: + = higher
    float stretch    = 0.0f;    // formant spacing, -1..1: - = squeezed towards F1, + = pushed apart
    float resonance  = 0.6f;    // 0..1, scales the formant bandwidths (0.6 = the table's bandwidths)
    float scatter    = 0.0f;    // 0 = cells sit on their formants, 1 = free, irregular positions

    // Organic layer
    Character character = Character::Warm;
    float feedback   = 0.0f;    // 0..1, wet signal back into the field; near 1 it self-oscillates
    float damping    = 0.3f;    // 0..1, darkens the feedback path
    bool  autoGain   = true;    // turn the wet signal down when it gets louder than the input (never up)
    bool  limiter    = true;    // soft safety ceiling at 0 dBFS

    // Flux: how alive the field is, calm (0) to chaotic (1). It drives every life process through
    // the macro (see FluxMacro): drift speed, lifespan, coupling and what feeds the life cycle.
    float fluxAmount = 0.25f;   // 0 = a still formant filter
    float wander     = 0.5f;    // how far the drift roams (times Flux)
    float rasp       = 0.0f;    // throat roughness: jitter + sub-octave growl (times Flux); 0 = clean

    // Tests only: a value >= 0 replaces what the Flux macro would set.
    struct MacroOverride { float driftHz = -1.0f, lifespanSec = -1.0f, coupling = -1.0f, feed = -1.0f; } macroOverride;

    float driveDb    = 0.0f;    // input saturation
    float mix        = 1.0f;    // dry/wet
    float outputDb   = 0.0f;
};

// Small, real-time safe random generator (xorshift32).
struct FastRandom
{
    uint32_t state = 0x9E3779B9u;
    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9E3779B9u; }
    float nextBipolar() noexcept   // uniform -1..1
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    float nextGaussianish() noexcept { return (nextBipolar() + nextBipolar() + nextBipolar()) * 1.0f; } // σ ≈ 1
};

// The Flux macro: one knob from calm to chaotic. Every life process follows it on its own curve.
//   Flux 0:  a still filter. Slow drift (0.05 Hz), long lives (12 s), no coupling, the life cycle
//            driven mostly by the input's energy (cells live where the sound is).
//   Flux 1:  fast drift (2.5 Hz), short lives (0.25 s), full coupling, more chance (cells flicker
//            in and out on their own).
// Life depth (how far dead cells fade) is Flux itself; Wander and Rasp are scaled by Flux too.
struct FluxMacro
{
    float driftHz, lifespanSec, coupling, feed;

    static FluxMacro at (float flux) noexcept
    {
        const float f = std::clamp (flux, 0.0f, 1.0f);
        return { 0.05f * std::pow (50.0f, f),                  // 0.05 → 2.5 Hz, exponential
                 12.0f * std::pow (0.25f / 12.0f, f),          // 12 s → 0.25 s, exponential
                 f * std::sqrt (f),                            // 0 → 1, gentle at first
                 0.8f - 0.5f * f };                            // energy-fed → more chance
    }
};

// A field of resonant cells grouped into formants: a throat that shapes any input into vowels.
// The cells are divided among 1–5 formants (by Density). The cells of one formant are slightly
// detuned around its centre, so a formant is a broad, uneven hump rather than one sharp peak.
// Vowel, Shift and Resonance pick the formants from the throat table; Scatter pulls the cells away
// from them to free, irregular positions. Coefficients are recomputed at control rate (every
// controlInterval samples) from smoothed settings and the modulation.
class FluxField
{
public:
    static constexpr int maxCells = 16;   // matches the Density parameter
    static constexpr int maxChannels = 2;
    static constexpr int controlInterval = 32;
    static constexpr float wetCeiling = 4.0f, invWetCeiling = 1.0f / wetCeiling;
    using Matrix = ModMatrix<maxCells>;

    // What the UI shows: written by the audio thread every control tick, read by the UI at any time.
    // Enough to draw the field's combined response: every cell (both channels are identical) plus
    // the gain after it, the feedback loop and the mix.
    struct Snapshot
    {
        std::atomic<int> activeCells { 0 };
        std::array<std::atomic<float>, maxCells> freq {}, q {}, level {};   // level: gain × life, before the norm
        std::atomic<float> norm { 1.0f }, post { 1.0f };          // inside the feedback loop / after it (makeup × auto gain)
        std::atomic<float> feedback { 0.0f }, dampHz { 20000.0f }, mix { 1.0f };
        std::atomic<float> flux { 0.0f };                         // Flux after modulation, 0..1
    };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        const float ticksPerSecond = sampleRate / (float) controlInterval;
        auto tickCoef = [ticksPerSecond] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * ticksPerSecond)); };
        smoothCoef   = tickCoef (0.025f);
        agSmooth     = tickCoef (0.25f);
        tickDt       = 1.0f / ticksPerSecond;
        envCoef      = 1.0f - std::exp (-1.0f / (0.3f * sampleRate));   // loudness detectors, per sample
        energyCoef   = tickCoef (0.02f);
        excAttack    = tickCoef (0.08f);
        excRelease   = tickCoef (1.5f);
        resetLife();
        resetRasp();
        dcCoef       = 1.0f - 2.0f * 3.14159265f * 10.0f / sampleRate; // DC blocker at ~10 Hz

        mod.prepare (sampleRate, ticksPerSecond);
        chaos.reset();
        agGain = 1.0f;
        levelMod.fill (1.0f);
        reset();
        firstTick = true;
        makeupCounter = 0;
    }

    void reset() noexcept
    {
        for (auto& ch : cells) for (auto& c : ch) c.reset();
        for (auto& ch : lastOut) ch.fill (0.0f);
        for (auto& ch : energyAcc) ch.fill (0.0f);
        energy.fill (0.0f); excitation.fill (0.0f); energySamples = 0;
        fbState.fill (0.0f); dampState.fill (0.0f); dcX.fill (0.0f); dcY.fill (0.0f);
        inPow = wetPow = 0.0f;
        samplesUntilTick = 0;
    }

    void setSettings (const FieldSettings& s) noexcept { target = s; }

    // Modulation: settings per block (audio thread), transport per block, shapes from any thread.
    void setModSettings (const ModSettings& s) noexcept { mod.setSettings (s); }
    void setTransport (const Transport& t) noexcept { mod.setTransport (t); }

    // In-place processing. right may be nullptr for mono.
    void process (float* left, float* right, int numSamples) noexcept
    {
        const int numCh = right != nullptr ? 2 : 1;
        float* chans[2] = { left, right };
        blockPeakPre.fill (0.0f);
        blockPeakPost.fill (0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            if (samplesUntilTick <= 0)
            {
                controlTick (i);
                samplesUntilTick = controlInterval;
            }
            --samplesUntilTick;

            float inSq = 0.0f, wetSq = 0.0f;
            float wetCh[2] = { 0.0f, 0.0f };

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = chans[ch][i];
                const float wet = blockDc (processWet (applyDrive (dry) + feedbackSignal (ch), ch), ch);
                fbState[(size_t) ch] = wet;
                wetCh[ch] = wet;
                inSq += dry * dry;
                wetSq += wet * wet * makeup * makeup;
            }

            energySamples += numCh;

            // Linked loudness detectors (keep the stereo image intact).
            inPow  += (inSq  / (float) numCh - inPow)  * envCoef;
            wetPow += (wetSq / (float) numCh - wetPow) * envCoef;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = chans[ch][i];
                // Soft wet ceiling at +12 dBFS (transparent below ~0 dBFS), even with the limiter off.
                const float wet = wetCeiling * softClip (wetCh[ch] * makeup * agGain * invWetCeiling);
                float out = (dry + (wet - dry) * effMix) * outGain;
                blockPeakPre[(size_t) ch] = std::max (blockPeakPre[(size_t) ch], std::abs (out));
                if (cur.limiter)
                    out = safetyLimit (out);

                if (! std::isfinite (out))   // last-resort guard: never pass NaN/inf to the host
                {
                    reset();
                    out = 0.0f;
                }
                chans[ch][i] = out;
                blockPeakPost[(size_t) ch] = std::max (blockPeakPost[(size_t) ch], std::abs (out));
            }
        }
    }

    // For tests / future visualiser (audio thread only).
    float getCellFrequency (int i) const noexcept { return cellFreq[(size_t) i]; }       // before modulation
    float getCellFrequencyNow (int i) const noexcept { return cellFreqNow[(size_t) i]; }
    float getCellQ (int i) const noexcept { return cellQ[(size_t) i]; }
    float getCellGain (int i) const noexcept { return cellGain[(size_t) i]; }
    int   getCellFormant (int i) const noexcept { return formantOf[(size_t) i]; }
    int   getActiveCells() const noexcept { return activeCells; }
    float getCellExcitation (int i) const noexcept { return excitation[(size_t) i]; }
    float getCellVitality (int i) const noexcept { return life[(size_t) i].vitality; }
    bool  isCellAlive (int i) const noexcept { return life[(size_t) i].alive; }
    uint32_t getBirths() const noexcept { return births; }
    float getMakeupDb() const noexcept { return 20.0f * std::log10 (makeup); }
    float getAutoGain() const noexcept { return agGain; }
    const Matrix& getMatrix() const noexcept { return mod; }
    const Snapshot& getSnapshot() const noexcept { return snapshot; }

    // Peaks of the last process() call: before the safety limiter (clip detection) and at the output.
    float getPeakBeforeLimiter (int ch) const noexcept { return blockPeakPre[(size_t) ch]; }
    float getPeakOut (int ch) const noexcept { return blockPeakPost[(size_t) ch]; }

    // Below 0.8 linear; above, a smooth knee that never exceeds ±1.
    static float safetyLimit (float x) noexcept
    {
        constexpr float knee = 0.8f, room = 1.0f - knee;
        const float a = std::abs (x);
        if (a <= knee) return x;
        const float y = knee + room * softClip ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }

private:
    float applyDrive (float x) const noexcept
    {
        if (driveBlend <= 0.0f)
            return x;
        const float driven = softClip (x * driveGain) * driveMakeup;
        return x + (driven - x) * driveBlend;
    }

    // The asymmetric saturation (Warm/Broken) produces a little DC; remove it from the wet path
    // before it reaches the output or the feedback loop.
    float blockDc (float x, int ch) noexcept
    {
        const auto c = (size_t) ch;
        const float y = x - dcX[c] + dcCoef * dcY[c];
        dcX[c] = x; dcY[c] = y;
        return y;
    }

    // Wet output fed back in: damped (one-pole lowpass) and saturated.
    float feedbackSignal (int ch) noexcept
    {
        if (fbGain <= 0.0f)
            return 0.0f;
        const auto c = (size_t) ch;
        dampState[c] += (fbState[c] - dampState[c]) * dampCoef;
        return softClip (dampState[c]) * fbGain;
    }

    float processWet (float x, int ch) noexcept
    {
        // Coupling, audio rate: each cell also hears its neighbours' last output (bleed), so a ringing
        // cell sets the cells next to it ringing, like sympathetic strings. The neighbour weights are
        // at most 1 per side, so the loop gain stays below 2 * bleed (< 0.5): it never runs away.
        auto& prev = lastOut[(size_t) ch];
        auto& acc = energyAcc[(size_t) ch];
        float sum = 0.0f, before = 0.0f;   // before: the previous cell's output from the last sample
        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            float in = x;
            if (bleed > 0.0f)
                in += bleed * (bleedLeft[ci] * before + bleedRight[ci] * prev[ci + 1]);   // bleedRight is 0 for the last cell
            const float y = cells[(size_t) ch][ci].process (in);
            // Rasp: amplitude jitter, and growl: every other cycle (rising zero crossings) is
            // quieter, which adds the sub-octave of a rough, fry-like voice.
            float heard = y;
            if (raspOn)
            {
                auto& r = raspState[(size_t) ch][ci];
                if (prev[ci] < 0.0f && y >= 0.0f)
                    r.oddCycle = ! r.oddCycle;
                r.amp += r.ampStep;
                heard = y * r.amp * (r.oddCycle ? growlLow : growlHigh);
            }
            before = prev[ci];
            prev[ci] = y;
            acc[ci] += y * y;
            sum += heard * cellGain[ci] * levelMod[ci];
        }
        return sum * fieldNorm;
    }

    void smoothTowardsTarget() noexcept
    {
        if (firstTick)
        {
            cur = target;
            firstTick = false;
            return;
        }
        auto glide = [this] (float& v, float t) { v += (t - v) * smoothCoef; };
        glide (cur.vowel, target.vowel);
        glide (cur.shiftOct, target.shiftOct);
        glide (cur.stretch, target.stretch);
        glide (cur.resonance, target.resonance);
        glide (cur.scatter, target.scatter);
        glide (cur.feedback, target.feedback);
        glide (cur.damping, target.damping);
        glide (cur.fluxAmount, target.fluxAmount);
        glide (cur.wander, target.wander);
        cur.macroOverride = target.macroOverride;
        glide (cur.rasp, target.rasp);
        glide (cur.driveDb, target.driveDb);
        glide (cur.mix, target.mix);
        glide (cur.outputDb, target.outputDb);
        cur.density = target.density;
        cur.character = target.character;
        cur.autoGain = target.autoGain;
        cur.limiter = target.limiter;
    }

    void updateAutoGain() noexcept
    {
        if (! cur.autoGain)
        {
            agGain += (1.0f - agGain) * agSmooth;
            return;
        }
        // Only adapt while there is real input; tails and feedback drones keep the last gain.
        if (inPow > 1.0e-7f)
        {
            // Protection only: when the wet signal gets louder than 3 dB under the input (resonance,
            // drive, feedback), turn it down, by up to 30 dB. Never turn it up: a filter that removes
            // part of the spectrum should sound quieter.
            const float targetAg = std::clamp (std::sqrt (inPow / (wetPow + 1.0e-12f)) * 0.708f, 0.0316f, 1.0f);
            agGain += (targetAg - agGain) * agSmooth;
        }
    }

    void publishSnapshot() noexcept
    {
        constexpr auto relaxed = std::memory_order_relaxed;
        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            snapshot.freq[ci].store (cellFreqNow[ci], relaxed);
            snapshot.q[ci].store (cellQ[ci], relaxed);
            snapshot.level[ci].store (cellGain[ci] * levelMod[ci], relaxed);
        }
        snapshot.activeCells.store (activeCells, relaxed);
        snapshot.norm.store (fieldNorm, relaxed);
        snapshot.post.store (makeup * agGain, relaxed);
        snapshot.feedback.store (fbGain, relaxed);
        snapshot.dampHz.store (dampHz, relaxed);
        snapshot.mix.store (effMix, relaxed);
        snapshot.flux.store (effFlux, relaxed);
    }

    // Global parameters after modulation (natural units).
    FieldSettings effectiveSettings() const noexcept
    {
        using spec::FP;
        auto m = [this] (FP p, float base, ModTarget t) { return Matrix::modulated (p, base, mod.global (t)); };
        FieldSettings e = cur;
        e.shiftOct  = m (FP::Shift, cur.shiftOct, ModTarget::Shift);
        e.stretch   = m (FP::Stretch, cur.stretch, ModTarget::Stretch);
        e.feedback  = m (FP::Feedback, cur.feedback, ModTarget::Feedback);
        e.damping   = m (FP::Damping, cur.damping, ModTarget::Damping);
        e.fluxAmount = m (FP::FluxAmount, cur.fluxAmount, ModTarget::FluxAmount);
        e.wander    = m (FP::Wander, cur.wander, ModTarget::Wander);
        e.rasp      = m (FP::Rasp, cur.rasp, ModTarget::Rasp);
        e.driveDb   = m (FP::Drive, cur.driveDb, ModTarget::Drive);
        e.mix       = m (FP::Mix, cur.mix, ModTarget::Mix);
        e.outputDb  = m (FP::Output, cur.outputDb, ModTarget::Output);
        return e;
    }

    // Resonance → bandwidth factor: 0.6 = the table's bandwidths, 0 = 12x wider, 1 = ~5x narrower.
    static float bandwidthScale (float r) noexcept { return std::exp2 ((0.6f - std::clamp (r, 0.0f, 1.0f)) * 6.0f); }

    static constexpr float minQ = 0.5f, maxQ = 64.0f;
    static constexpr float maxDetuneOct = 1.0f / 6.0f;   // widest spread of one formant's cells
    static constexpr float jitterShare = 0.15f;          // each cell's own offset, as a share of the formant's width
    static constexpr float freeLowOct = 6.644f;          // log2 (100 Hz): free cells live in 100 Hz…
    static constexpr float freeSpanOct = 5.907f;         // …6 kHz (before Shift)

    // Fixed per-cell character, rolled once per cell index (so presets sound the same every time).
    struct CellTraits
    {
        float jitter = 0.0f;     // -1..1, own detune
        float freePos = 0.5f;    // 0..1, where it sits when scattered
        float freeQ = 0.0f;      // -1..1, octaves of Q around the free cells' base Q
        float freeGainDb = 0.0f; // level when scattered
    };

    static CellTraits rollTraits (int c) noexcept
    {
        FastRandom rng; rng.seed (0x51ED27u + 7919u * (uint32_t) c);
        CellTraits t;
        t.jitter = rng.nextBipolar();
        t.freePos = 0.5f + 0.5f * rng.nextBipolar();
        t.freeQ = rng.nextBipolar();
        t.freeGainDb = -4.5f + 4.5f * rng.nextBipolar();
        return t;
    }

    // Divides n cells among the formants: as even as possible, the lower formants get the extra
    // cells. Cells stay sorted by formant, so the field reads from low to high.
    void assignFormants (int n) noexcept
    {
        numFormants = formants::countFor (n);
        const int base = n / numFormants, extra = n % numFormants;
        int c = 0;
        for (int k = 0; k < numFormants; ++k)
        {
            const int m = base + (k < extra ? 1 : 0);
            cellsInFormant[(size_t) k] = m;
            for (int j = 0; j < m; ++j, ++c)
            {
                formantOf[(size_t) c] = k;
                // Position inside the formant, -0.5..0.5 (0 for a lone cell).
                spreadPos[(size_t) c] = m > 1 ? (float) j / (float) (m - 1) - 0.5f : 0.0f;
            }
        }
        assignedCells = n;

        // Bleed neighbours: the cells next to each other in the field; across a formant border at half weight.
        for (int i = 0; i < n; ++i)
        {
            const auto ci = (size_t) i;
            bleedLeft[ci]  = i > 0     ? (formantOf[ci - 1] == formantOf[ci] ? 1.0f : 0.5f) : 0.0f;
            bleedRight[ci] = i + 1 < n ? (formantOf[ci + 1] == formantOf[ci] ? 1.0f : 0.5f) : 0.0f;
        }
    }

    struct Placement { float octs, q, gainDb; };

    // Where cell c sits for a vowel position, shift, bandwidth factor and scatter amount.
    Placement place (int c, float vowel, float shiftOct, float bwScale, float scatter) const noexcept
    {
        const auto k = formantOf[(size_t) c];
        auto fm = formants::at (vowel, k);
        // Stretch: the spacing of the formants above F1 (in octaves) times 0.6…1.6. The bandwidth
        // scales with the frequency, so a formant keeps its shape.
        if (stretchFactor != 1.0f && k > 0)
        {
            const float f1 = formants::at (vowel, 0).hz;
            const float moved = f1 * std::exp2 (stretchFactor * std::log2 (fm.hz / f1));
            fm.bwHz *= moved / fm.hz;
            fm.hz = moved;
        }
        const auto& tr = traits[(size_t) c];
        const float bw = fm.bwHz * bwScale;

        // A formant's cells fan out over about half its bandwidth (at most 1/6 octave), plus a small
        // offset of their own. Both scale with the bandwidth: a narrow (singing) formant pulls its
        // cells together into one coherent peak; spread wider than their own bandwidth, detuned
        // cells would partly cancel each other.
        const float bwOct = std::log2 ((fm.hz + 0.5f * bw) / std::max (1.0f, fm.hz - 0.5f * bw));
        const float span = std::min (0.5f * bwOct, maxDetuneOct);
        // Life: each formant drifts on its own; openness (jaw) moves F1 most.
        const float driftOct = formantDrift[(size_t) k] + openness * (k == 0 ? opennessF1Oct : opennessOct);
        // Life: a reborn cell lands a little elsewhere in its formant (up to its whole span, times Flux).
        const auto& lf = life[(size_t) c];
        const float formantOct = std::log2 (fm.hz) + driftOct
                               + span * (spreadPos[(size_t) c] + jitterShare * tr.jitter + lifeDepth * lf.shift);
        // Two resonant stages: a cell's -3 dB width is about 0.64 f / Q.
        const float formantQ = std::clamp (0.64f * fm.hz / bw, minQ, maxQ);
        // Louder formants have more cells; share the formant's level between them.
        const float formantDb = fm.gainDb - 10.0f * std::log10 ((float) cellsInFormant[(size_t) k]);

        float freePos = tr.freePos + 0.5f * lifeDepth * lf.freeShift;   // scattered: anywhere
        freePos -= std::floor (freePos);
        const float freeOct = freeLowOct + freeSpanOct * freePos;
        const float freeQ = std::clamp (4.0f / bwScale * std::exp2 (tr.freeQ), minQ, maxQ);

        const float s = std::clamp (scatter, 0.0f, 1.0f);
        return { formantOct + (freeOct - formantOct) * s + shiftOct,
                 std::exp2 (std::log2 (formantQ) + (std::log2 (freeQ) - std::log2 (formantQ)) * s),
                 formantDb + (tr.freeGainDb - formantDb) * s };
    }

    // Coupling, control rate: each cell's energy (its output level, ~20 ms) relative to the field's
    // average. A cell whose neighbours ring harder than average gets excited: louder (up to +6 dB)
    // and narrower (Q up to 1.5x). It rises in ~80 ms and lets go over ~1.5 s, so resonance spreads
    // outward from where the input hits and wanders across the field. The bleed (audio rate) is
    // set here too.
    void updateCoupling (float amount) noexcept
    {
        const float n = (float) std::max (1, energySamples);
        float mean = 0.0f;
        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            const float level = (energyAcc[0][ci] + energyAcc[1][ci]) / n;
            energy[ci] += (level - energy[ci]) * energyCoef;
            energyAcc[0][ci] = energyAcc[1][ci] = 0.0f;
            mean += energy[ci];
        }
        energySamples = 0;
        mean = mean / (float) activeCells + 1.0e-9f;
        energyMean = mean;

        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            const float wl = bleedLeft[ci], wr = bleedRight[ci];
            const float drive = (wl * (c > 0 ? energy[ci - 1] : 0.0f) + wr * (c + 1 < activeCells ? energy[ci + 1] : 0.0f))
                              / ((wl + wr + 1.0e-6f) * mean);
            const float targetExc = amount * std::clamp (0.5f * (drive - 1.0f), 0.0f, 1.0f);
            auto& ex = excitation[ci];
            ex += (targetExc - ex) * (targetExc > ex ? excAttack : excRelease);
            excitedGain[ci] = 1.0f + maxExcitedGain * ex;
            excitedQ[ci] = 1.0f + maxExcitedQ * ex;
        }

        // The excited gains go into the level normalisation, and the bleed barely changes the level
        // (measured: tones and pink noise within ±1 dB of no coupling), so no extra compensation.
        bleed = maxBleed * amount;
    }

    // Life cycle. Each cell is alive (vitality rises to 1) or dead (vitality falls to 0, silent),
    // and hunger decides when it dies:
    //   chance (Feed 0): hunger grows by itself; the cell dies after its own lifespan (0.5–1.5×
    //                    Lifespan) and is reborn after a random rest (0.15–0.55× Lifespan).
    //   energy (Feed 1): a cell eats the input energy in its band. Fed cells (own energy well above
    //                    a floor and a quarter of the field's average, or excited by their neighbours)
    //                    lose hunger, starved ones die after about Lifespan. A dead cell keeps
    //                    listening at its new place and is reborn as soon as there is food there.
    //                    Silence starves everything: the population dies out.
    // Feed blends both rates. Reborn cells land elsewhere (see place()). Flux sets the depth: the
    // cell's gain is 1 - Flux × (1 - vitality), so at Flux 0 every cell lives forever.
    struct CellLife
    {
        bool  alive = true;
        float vitality = 1.0f, hunger = 0.0f, rebirth = 0.0f;
        float span = 1.0f, rest = 0.5f;       // own lifespan / rest, as multiples of Lifespan
        float shift = 0.0f, freeShift = 0.0f; // where it lands, -1..1
        bool  moved = false;                  // dead and already moved to its next place
    };

    void resetLife() noexcept
    {
        lifeRng.seed (0xC0FFEE11u);
        for (auto& l : life)
        {
            l = CellLife {};
            rollLife (l, lifeRng);
            l.shift = l.freeShift = 0.0f;                        // the first generation sits on the knobs' layout
            l.hunger = 0.5f * (lifeRng.nextBipolar() + 1.0f);    // staggered, so they don't all die together
        }
        births = 0;
    }

    static void rollLife (CellLife& l, FastRandom& rng) noexcept
    {
        l.span = 1.0f + 0.5f * rng.nextBipolar();
        l.rest = 0.35f + 0.2f * rng.nextBipolar();
        l.shift = rng.nextBipolar();
        l.freeShift = rng.nextBipolar();
    }

    void updateLife (float depth, float feedAmount, float lifespan) noexcept
    {
        lifeDepth = depth;
        const float dt = tickDt;
        const float attack = std::clamp (0.15f * lifespan, 0.005f, 1.5f);
        const float release = std::clamp (0.3f * lifespan, 0.01f, 3.0f);
        const float attackCoef = 1.0f - std::exp (-dt / attack), releaseCoef = 1.0f - std::exp (-dt / release);
        const float foodFloor = std::max (minFood, 0.25f * energyMean);

        for (int c = 0; c < activeCells; ++c)
        {
            auto& l = life[(size_t) c];
            const float e = energy[(size_t) c];
            const float fed = std::max (e / (e + foodFloor), excitation[(size_t) c]);   // 0 starving .. 1 well fed

            if (l.alive)
            {
                const float chanceRate = 1.0f / (l.span * lifespan);
                const float energyRate = (1.0f - 2.0f * fed) / lifespan;        // fed > 0.5 recovers
                l.hunger = std::max (0.0f, l.hunger + dt * ((1.0f - feedAmount) * chanceRate + feedAmount * energyRate));
                l.vitality += (1.0f - l.vitality) * attackCoef;
                if (l.hunger >= 1.0f)
                {
                    l.alive = false;
                    l.rebirth = 0.0f;
                }
            }
            else
            {
                l.vitality -= l.vitality * releaseCoef;
                if (l.vitality < 0.02f && ! l.moved)
                {
                    rollLife (l, lifeRng);   // silent now: move it to where it will be reborn
                    l.moved = true;
                }
                const float chancePush = 1.0f / (l.rest * lifespan);
                const float energyPush = fed > 0.5f ? 1.0f / minRebirthSec : 0.0f;
                if (l.moved)
                    l.rebirth += dt * ((1.0f - feedAmount) * chancePush + feedAmount * energyPush);
                if (l.rebirth >= 1.0f)
                {
                    l.alive = true;
                    l.moved = false;
                    l.hunger = 0.0f;
                    ++births;
                }
            }
        }
    }

    // Rasp: the roughness of a throat.
    //   Jitter: every cell's frequency (up to ±0.08 oct) and level (up to ±50 %) shake with their own
    //           band-limited noise (value noise at ~180 Hz, two layers), updated every control tick
    //           (1.5 kHz at 48 kHz) and, for the level, ramped per sample.
    //   Growl:  every other cycle of each cell is turned down (up to 80 %), the period doubles and a
    //           sub-octave appears, like vocal fry. Both halves are scaled so the power stays the same.
    struct RaspState
    {
        float amp = 1.0f, ampStep = 0.0f;
        bool  oddCycle = false;
    };

    void resetRasp() noexcept
    {
        for (int c = 0; c < maxCells; ++c)
        {
            const auto seed = 0xA5A5F00Du + 131u * (uint32_t) c;
            raspFreqNoise[(size_t) c].seed (seed);
            raspAmpNoise[(size_t) c].seed (seed ^ 0x5bd1e995u);
            raspOct[(size_t) c] = 0.0f;
            for (auto& ch : raspState) ch[(size_t) c] = {};
        }
        raspOn = false;
    }

    void updateRasp (float amount) noexcept
    {
        const bool wasOn = raspOn;
        raspOn = amount > 0.0f;
        const float growl = maxGrowl * amount;
        const float low = 1.0f - growl;
        const float keep = std::sqrt (2.0f / (1.0f + low * low));   // same power as without growl
        growlHigh = keep;
        growlLow = keep * low;

        // Both channels shake the same way (the stereo image stays as it came in); each channel
        // only counts its own cycles for the growl.
        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            if (! raspOn)
            {
                raspOct[ci] = 0.0f;
                for (auto& ch : raspState) ch[ci] = {};
                continue;
            }
            if (! wasOn)
                for (auto& ch : raspState) ch[ci] = {};
            raspOct[ci] = amount * maxRaspOct * raspFreqNoise[ci].advance (raspRateHz, tickDt);
            const float targetAmp = 1.0f + amount * maxRaspAmp * raspAmpNoise[ci].advance (raspRateHz, tickDt);
            for (auto& ch : raspState)
                ch[ci].ampStep = (targetAmp - ch[ci].amp) / (float) controlInterval;
        }
    }

    void controlTick (int sampleOffset) noexcept
    {
        smoothTowardsTarget();
        updateAutoGain();

        mod.tickSources (tickDt, sampleOffset);
        mod.computeSlots();
        const FieldSettings e = effectiveSettings();

        // Flux: one knob from calm to chaotic. The macro sets every life process (tests may override).
        const float flux = std::clamp (e.fluxAmount, 0.0f, 1.0f);
        effFlux = flux;
        FluxMacro mc = FluxMacro::at (flux);
        const auto& ov = cur.macroOverride;
        if (ov.driftHz >= 0.0f)     mc.driftHz = ov.driftHz;
        if (ov.lifespanSec >= 0.0f) mc.lifespanSec = ov.lifespanSec;
        if (ov.coupling >= 0.0f)    mc.coupling = ov.coupling;
        if (ov.feed >= 0.0f)        mc.feed = ov.feed;

        // Life: the chaos moves the throat. Flux × Wander sets how far; 0 = still.
        // Both channels share one chaos, one population and one rasp: the stereo image stays as it came in.
        const float roam = std::clamp (flux * e.wander, 0.0f, 1.0f);
        stretchFactor = std::exp2 (maxStretchOct * std::clamp (e.stretch, -1.0f, 1.0f));
        const auto& chaosOut = chaos.advance (mc.driftHz, tickDt);
        const float vowelNow = cur.vowel + roam * maxVowelWander * chaosOut.vowel;
        const float shiftNow = e.shiftOct + roam * maxShiftWanderOct * chaosOut.size;
        openness = roam * chaosOut.openness;
        for (size_t k = 0; k < formants::numFormants; ++k)
            formantDrift[k] = roam * maxFormantWanderOct * chaosOut.formant[k];

        // Energy coupling, life cycle and rasp, all driven by Flux.
        updateCoupling (std::clamp (mc.coupling, 0.0f, 1.0f));
        updateLife (flux, std::clamp (mc.feed, 0.0f, 1.0f), std::max (0.01f, mc.lifespanSec));
        updateRasp (std::clamp (flux * e.rasp, 0.0f, 1.0f));
        effMix = e.mix;

        // Broken: coefficients only update every 8th tick, so movement steps instead of gliding.
        const bool broken = cur.character == Character::Broken;
        const bool updateCoefficients = ! broken || (tickCounter++ % 8) == 0;

        const int newCount = std::clamp (cur.density, 1, maxCells);
        for (int c = activeCells; c < newCount; ++c)   // newly woken cells start silent
        {
            for (int ch = 0; ch < maxChannels; ++ch)
                cells[(size_t) ch][(size_t) c].reset();
            cellGain[(size_t) c] = 0.0f;
        }
        activeCells = newCount;
        if (assignedCells != activeCells)
            assignFormants (activeCells);

        // LFO connections with a Flux share: the living cells absorb them (last tick's life gains),
        // in lockstep at Flux 0 and each in its own way at Flux 1.
        mod.absorb (levelMod, activeCells, flux);

        const float maxHz = 0.45f * sampleRate;
        constexpr float vowelRange = (float) formants::numShapes;   // the Vowel knob spans the whole ring

        float satLimit = 4.0f, satAsym = 0.0f;
        switch (cur.character)
        {
            // Limits are in output units: where the resonant peak starts to bend.
            case Character::Clean:  satLimit = 4.0f;  satAsym = 0.0f;  break;
            case Character::Warm:   satLimit = 1.0f;  satAsym = 0.35f; break;
            case Character::Broken: satLimit = 0.5f;  satAsym = 0.6f;  break;
        }

        for (int c = 0; c < activeCells; ++c)
        {
            const auto ci = (size_t) c;
            // Level and the unmodulated position come from the knobs alone.
            const auto base = place (c, vowelNow, shiftNow, bandwidthScale (cur.resonance), cur.scatter);
            const float f = std::exp2 (base.octs);
            cellFreq[ci] = f;
            // Cells pushed above the usable range fade out instead of piling up at Nyquist.
            targetGain[ci] = f < maxHz ? std::pow (10.0f, base.gainDb / 20.0f) * excitedGain[ci] : 0.0f;
            levelMod[ci] = 1.0f - lifeDepth * (1.0f - life[ci].vitality);

            if (! updateCoefficients)
                continue;

            // Per-cell modulation.
            const float vowelOff = mod.cell (ModTarget::Vowel, c);
            const float resOff = mod.cell (ModTarget::Resonance, c);
            const float scatterOff = mod.cell (ModTarget::Scatter, c);
            const auto pl = (vowelOff == 0.0f && resOff == 0.0f && scatterOff == 0.0f)
                ? base
                : place (c, vowelNow + vowelRange * vowelOff, shiftNow, bandwidthScale (cur.resonance + resOff), cur.scatter + scatterOff);

            float octs = pl.octs;
            if (broken)
                octs = std::round (octs * 4.0f) * 0.25f;   // cutoffs snap to a quarter-octave grid
            octs += raspOct[ci];
            const float hz = std::clamp (std::exp2 (octs), 15.0f, maxHz);
            cellFreqNow[ci] = hz;
            const float q = std::min (maxQ, pl.q * excitedQ[ci]);
            cellQ[ci] = q;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                auto& cell = cells[(size_t) ch][ci];
                cell.setCoefficients (hz, q, sampleRate);
                // The band state grows with Q (unity peak output = state / Q), so the limit scales
                // with Q too: saturation then starts at the same output level at any resonance.
                cell.setSaturation (satLimit * q, satAsym);
            }
        }

        for (int c = 0; c < activeCells; ++c)
            cellGain[(size_t) c] += (targetGain[(size_t) c] - cellGain[(size_t) c]) * smoothCoef;

        // Level: a filter must never amplify. Find the field's loudest point (every cell centre plus
        // one octave beyond each end, summing all cells as magnitudes) and scale it to unity.
        // Recomputed every 4th tick; drops at once, recovers smoothly.
        if (updateCoefficients && (normCounter++ % 4) == 0)
        {
            // Life: the living cells are normalised (the population's level stays even as cells die),
            // but by at most +12 dB over the full field, so a dying population still fades out.
            float loudest = 1.0e-6f, loudestFull = 1.0e-6f;
            auto probe = [&] (float hz) {
                float sum = 0.0f, full = 0.0f;
                for (int c = 0; c < activeCells; ++c)
                {
                    const float m = cellGain[(size_t) c] * FluxCell::magnitude (hz / cellFreqNow[(size_t) c], cellQ[(size_t) c], 1.0f);
                    sum += m * levelMod[(size_t) c];
                    full += m;
                }
                loudest = std::max (loudest, sum);
                loudestFull = std::max (loudestFull, full);
            };
            float lowest = maxHz, highest = 0.0f;
            for (int c = 0; c < activeCells; ++c)
            {
                const float f = cellFreqNow[(size_t) c];
                probe (f);
                lowest = std::min (lowest, f);
                highest = std::max (highest, f);
            }
            probe (lowest * 0.5f);
            probe (std::min (highest * 2.0f, maxHz));
            const float unityNorm = std::min (1.0f / loudest, maxPopulationLift / loudestFull);
            fieldNorm = unityNorm < fieldNorm ? unityNorm : fieldNorm + (unityNorm - fieldNorm) * 0.2f;
        }

        // Makeup: narrow formants pass little of a broadband sound. Estimate how much of a pink input
        // (equal energy per octave, 20 Hz–20 kHz) gets through and bring that up to 3 dB under the
        // input, by at most +12 dB, so a tone on a formant peak is never more than 12 dB louder than
        // it came in (Auto Gain and the limiter catch the rest). Applied after the feedback loop.
        if (updateCoefficients && (makeupCounter++ % 16) == 0)
        {
            float pinkEnergy = 0.0f;
            for (int i = 0; i < makeupPoints; ++i)
            {
                const float hz = 20.0f * std::exp2 ((float) i / makeupPointsPerOct);
                if (hz >= maxHz) continue;
                // Complex sum: neighbouring formants are out of phase between their peaks and
                // partly cancel there (a magnitude sum would over-estimate what gets through).
                const auto sum = response (hz, activeCells, cellFreqNow.data(), cellQ.data(),
                                           [this] (int c) { return cellGain[(size_t) c] * levelMod[(size_t) c]; });
                pinkEnergy += std::norm (sum);
            }
            const float passedDb = 10.0f * std::log10 (std::max (1.0e-12f, fieldNorm * fieldNorm * pinkEnergy / (float) makeupPoints));
            makeupTarget = std::pow (10.0f, std::clamp (makeupTargetDb - passedDb, 0.0f, maxMakeupDb) / 20.0f);
        }
        makeup += (makeupTarget - makeup) * smoothCoef;

        // Feedback goes above unity at the top so the field can sing on its own;
        // the saturation in the loop and in the cells keeps that bounded.
        fbGain = 1.35f * e.feedback;   // the field peaks just under unity, so the top of the range self-oscillates
        dampHz = std::min (20000.0f * std::exp2 (-e.damping * 5.3f), maxHz);
        dampCoef = 1.0f - std::exp (-2.0f * 3.14159265f * dampHz / sampleRate);

        publishSnapshot();

        driveGain = std::pow (10.0f, e.driveDb / 20.0f);
        driveMakeup = 1.0f / std::sqrt (driveGain);
        driveBlend = std::min (1.0f, e.driveDb / 6.0f);
        outGain = std::pow (10.0f, e.outputDb / 20.0f);
    }

public:
    // The field's complex response at hz (before the norm): the sum of every cell, two bandpass
    // stages at the same Q each: (j k x / (1 - x² + j k x))². level (c) gives each cell's gain.
    // Shared by the makeup estimate and the display.
    template <typename Level>
    static std::complex<float> response (float hz, int numCells, const float* freq, const float* q, Level&& level) noexcept
    {
        std::complex<float> sum {};
        for (int c = 0; c < numCells; ++c)
        {
            const float x = hz / freq[c], k = 1.0f / q[c];
            const std::complex<float> stage = std::complex<float> (0.0f, k * x) / std::complex<float> (1.0f - x * x, k * x);
            sum += level (c) * stage * stage;
        }
        return sum;
    }

private:
    float sampleRate = 44100.0f;
    float smoothCoef = 1.0f, agSmooth = 1.0f, tickDt = 0.001f;
    float envCoef = 0.001f, dcCoef = 0.999f;
    bool  firstTick = true;
    int   samplesUntilTick = 0;
    uint32_t tickCounter = 0;

    FieldSettings target, cur;
    Matrix mod;
    float effMix = 1.0f, effFlux = 0.0f;
    std::array<float, maxChannels> blockPeakPre {}, blockPeakPost {};

    int activeCells = 0;
    std::array<std::array<FluxCell, maxCells>, maxChannels> cells {};
    std::array<float, maxCells> cellFreq {}, cellGain {}, targetGain {};
    std::array<float, maxCells> cellFreqNow {}, cellQ {}, levelMod {};
    uint32_t normCounter = 0;
    Snapshot snapshot;

    // Formant layout
    const std::array<CellTraits, maxCells> traits = [] {
        std::array<CellTraits, maxCells> t {};
        for (int c = 0; c < maxCells; ++c) t[(size_t) c] = rollTraits (c);
        return t;
    }();
    int numFormants = 1, assignedCells = 0;
    std::array<int, maxCells> formantOf {};
    std::array<int, formants::numFormants> cellsInFormant {};
    std::array<float, maxCells> spreadPos {};

    float fieldNorm = 1.0f;

    // Life: chaotic drift
    static constexpr float maxVowelWander = 2.0f;        // ring positions (shapes) either way
    static constexpr float maxShiftWanderOct = 0.25f;
    static constexpr float maxStretchOct = 0.7f;   // Stretch ±1 = formant spacing ×1.62 / ×0.62
    float stretchFactor = 1.0f;
    static constexpr float maxFormantWanderOct = 1.0f;   // each formant on its own
    static constexpr float opennessF1Oct = 0.4f, opennessOct = 0.12f;   // jaw: F1 moves most
    FluxChaos chaos;
    float openness = 0.0f;
    std::array<float, formants::numFormants> formantDrift {};

    // Life: cycle
    static constexpr float maxPopulationLift = 4.0f;   // +12 dB: how far the norm lifts a thinned-out population
    static constexpr float minFood = 1.0e-7f;          // energy below this (about -70 dB) is no food
    static constexpr float minRebirthSec = 0.05f;      // energy-driven rebirth takes at least this long
    std::array<CellLife, maxCells> life {};

    // Life: rasp
    static constexpr float maxRaspOct = 0.08f, maxRaspAmp = 0.5f, maxGrowl = 0.8f, raspRateHz = 180.0f;
    std::array<SmoothNoise, maxCells> raspFreqNoise, raspAmpNoise;
    std::array<std::array<RaspState, maxCells>, maxChannels> raspState {};   // per channel: its own cycles
    std::array<float, maxCells> raspOct {};
    bool  raspOn = false;
    float growlHigh = 1.0f, growlLow = 1.0f;
    FastRandom lifeRng;
    float lifeDepth = 0.0f, energyMean = 0.0f;
    uint32_t births = 0;

    // Life: energy coupling
    static constexpr float maxBleed = 0.25f;
    static constexpr float maxExcitedGain = 1.0f, maxExcitedQ = 0.5f;   // +6 dB, 1.5x Q at full excitation
    std::array<std::array<float, maxCells + 1>, maxChannels> lastOut {};   // +1: the last cell reads a silent right neighbour
    std::array<std::array<float, maxCells>, maxChannels> energyAcc {};
    std::array<float, maxCells> energy {}, excitation {}, bleedLeft {}, bleedRight {};
    std::array<float, maxCells> excitedGain = filled (1.0f), excitedQ = filled (1.0f);
    int energySamples = 0;
    float bleed = 0.0f;
    float energyCoef = 1.0f, excAttack = 1.0f, excRelease = 1.0f;
    static constexpr std::array<float, maxCells> filled (float v) { std::array<float, maxCells> a {}; a.fill (v); return a; }
    static constexpr int makeupPointsPerOct = 6, makeupPoints = 10 * makeupPointsPerOct + 1;   // 20 Hz … 20.5 kHz
    static constexpr float makeupTargetDb = -3.0f, maxMakeupDb = 12.0f;
    float makeup = 1.0f, makeupTarget = 1.0f;
    uint32_t makeupCounter = 0;
    float driveGain = 1.0f, driveMakeup = 1.0f, driveBlend = 0.0f, outGain = 1.0f;

    // Feedback loop
    std::array<float, maxChannels> fbState {}, dampState {}, dcX {}, dcY {};
    float fbGain = 0.0f, dampCoef = 1.0f, dampHz = 20000.0f;

    // Auto-gain
    float inPow = 0.0f, wetPow = 0.0f, agGain = 1.0f;
};

} // namespace flux

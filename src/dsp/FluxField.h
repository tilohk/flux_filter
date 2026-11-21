#pragma once

#include "FluxCell.h"
#include "FluxChaos.h"
#include "FluxPropagator.h"
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
    float sizeOct    = 0.0f;    // throat size, -2..+2 octaves: + = bigger throat = lower formants
    float resonance  = 0.6f;    // 0..1, scales the formant bandwidths (0.6 = the table's bandwidths)
    float scatter    = 0.0f;    // 0 = cells sit on their formants, 1 = free, irregular positions
    float width      = 0.5f;    // stereo: L/R cutoff offset + alternating pan

    // Organic layer
    Character character = Character::Warm;
    float feedback   = 0.0f;    // 0..1, wet signal back into the field; near 1 it self-oscillates
    float damping    = 0.3f;    // 0..1, darkens the feedback path
    bool  autoGain   = true;    // turn the wet signal down when it gets louder than the input (never up)
    bool  limiter    = true;    // soft safety ceiling at 0 dBFS

    // Flux: how alive the field is. Life so far: chaotic drift of the throat (phase 2).
    float fluxAmount = 0.25f;   // 0 = a still formant filter; scales every life process
    float driftHz    = 0.2f;    // speed of the chaos
    float wander     = 0.5f;    // how far it roams (times Flux)
    // How an LFO's Flux share travels through the cells (until v2 phase 7 redefines it).
    PropagationSettings propagation;

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

// A field of resonant cells grouped into formants: a throat that shapes any input into vowels.
// The cells are divided among 1–5 formants (by Density). The cells of one formant are slightly
// detuned around its centre, so a formant is a broad, uneven hump rather than one sharp peak.
// Vowel, Size and Resonance pick the formants from the throat table; Scatter pulls the cells away
// from them to free, irregular positions. Coefficients are recomputed at control rate (every
// controlInterval samples) from smoothed settings and the modulation.
class FluxField
{
public:
    static constexpr int maxCells = 16;   // matches the Density parameter
    static constexpr int maxChannels = 2;
    static constexpr int controlInterval = 32;
    static constexpr float wetCeiling = 4.0f, invWetCeiling = 1.0f / wetCeiling;
    using Matrix = ModMatrix<maxCells, maxChannels>;

    // What the UI shows: written by the audio thread every control tick, read by the UI at any time.
    struct Snapshot
    {
        std::atomic<int> activeCells { 0 };
        std::array<std::array<std::atomic<float>, maxCells>, maxChannels> freq {}, q {}, level {};
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
        dcCoef       = 1.0f - 2.0f * 3.14159265f * 10.0f / sampleRate; // DC blocker at ~10 Hz

        mod.prepare (sampleRate, ticksPerSecond);
        chaos.reset();
        agGain = 1.0f;
        for (auto& ch : levelMod) ch.fill (1.0f);
        reset();
        firstTick = true;
        makeupCounter = 0;
    }

    void reset() noexcept
    {
        for (auto& ch : cells) for (auto& c : ch) c.reset();
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
        stereo = numCh == 2;
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
    float getCellFrequency (int i) const noexcept { return cellFreq[(size_t) i]; }       // before modulation and width
    float getCellFrequencyNow (int i, int ch = 0) const noexcept { return cellFreqNow[(size_t) ch][(size_t) i]; }
    float getCellQ (int i, int ch = 0) const noexcept { return cellQ[(size_t) ch][(size_t) i]; }
    float getCellGain (int i) const noexcept { return cellGain[(size_t) i]; }
    int   getCellFormant (int i) const noexcept { return formantOf[(size_t) i]; }
    int   getActiveCells() const noexcept { return activeCells; }
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
        float sum = 0.0f;
        for (int c = 0; c < activeCells; ++c)
            sum += cells[(size_t) ch][(size_t) c].process (x) * cellGain[(size_t) c]
                 * levelMod[(size_t) ch][(size_t) c]
                 * (stereo ? pan[(size_t) ch][(size_t) c] : 1.0f);
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
        glide (cur.sizeOct, target.sizeOct);
        glide (cur.resonance, target.resonance);
        glide (cur.scatter, target.scatter);
        glide (cur.width, target.width);
        glide (cur.feedback, target.feedback);
        glide (cur.damping, target.damping);
        glide (cur.fluxAmount, target.fluxAmount);
        glide (cur.wander, target.wander);
        cur.driftHz = target.driftHz;
        glide (cur.propagation.travelSec, target.propagation.travelSec);
        glide (cur.propagation.viscosity, target.propagation.viscosity);
        glide (cur.propagation.wavelength, target.propagation.wavelength);
        glide (cur.propagation.turbulence, target.propagation.turbulence);
        glide (cur.propagation.stereo, target.propagation.stereo);
        cur.propagation.direction = target.propagation.direction;
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
        snapshot.activeCells.store (activeCells, relaxed);
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int c = 0; c < activeCells; ++c)
            {
                const auto cc = (size_t) ch, ci = (size_t) c;
                snapshot.freq[cc][ci].store (cellFreqNow[cc][ci], relaxed);
                snapshot.q[cc][ci].store (cellQ[cc][ci], relaxed);
                snapshot.level[cc][ci].store (cellGain[ci] * levelMod[cc][ci], relaxed);
            }
    }

    // Global parameters after modulation (natural units).
    FieldSettings effectiveSettings() const noexcept
    {
        using spec::FP;
        auto m = [this] (FP p, float base, ModTarget t) { return Matrix::modulated (p, base, mod.global (t)); };
        FieldSettings e = cur;
        e.sizeOct   = m (FP::Size, cur.sizeOct, ModTarget::Size);
        e.width     = m (FP::Width, cur.width, ModTarget::Width);
        e.feedback  = m (FP::Feedback, cur.feedback, ModTarget::Feedback);
        e.damping   = m (FP::Damping, cur.damping, ModTarget::Damping);
        e.fluxAmount = m (FP::FluxAmount, cur.fluxAmount, ModTarget::FluxAmount);
        e.driftHz   = m (FP::Drift, cur.driftHz, ModTarget::Drift);
        e.wander    = m (FP::Wander, cur.wander, ModTarget::Wander);
        e.propagation.viscosity  = m (FP::Viscosity, cur.propagation.viscosity, ModTarget::Viscosity);
        e.propagation.wavelength = m (FP::Wavelength, cur.propagation.wavelength, ModTarget::Wavelength);
        e.propagation.turbulence = m (FP::Turbulence, cur.propagation.turbulence, ModTarget::Turbulence);
        e.propagation.stereo     = m (FP::FluxStereo, cur.propagation.stereo, ModTarget::FluxStereo);
        e.driveDb   = m (FP::Drive, cur.driveDb, ModTarget::Drive);
        e.mix       = m (FP::Mix, cur.mix, ModTarget::Mix);
        e.outputDb  = m (FP::Output, cur.outputDb, ModTarget::Output);
        return e;
    }

    // Resonance → bandwidth factor: 0.6 = the table's bandwidths, 0 = 12x wider, 1 = ~5x narrower.
    static float bandwidthScale (float r) noexcept { return std::exp2 ((0.6f - std::clamp (r, 0.0f, 1.0f)) * 6.0f); }

    static constexpr float minQ = 0.5f, maxQ = 64.0f;
    static constexpr float maxDetuneOct = 1.0f / 6.0f;   // widest spread of one formant's cells
    static constexpr float maxWidthOct = 0.1f;           // Width's widest L/R detune
    static constexpr float jitterShare = 0.15f;          // each cell's own offset, as a share of the formant's width
    static constexpr float freeLowOct = 6.644f;          // log2 (100 Hz): free cells live in 100 Hz…
    static constexpr float freeSpanOct = 5.907f;         // …6 kHz (before Size)

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
    }

    struct Placement { float octs, q, gainDb, widthOct; };   // widthOct: how far Width may detune L/R

    // Where cell c sits for a vowel position, size, bandwidth factor and scatter amount.
    Placement place (int c, float vowel, float sizeOct, float bwScale, float scatter) const noexcept
    {
        const auto k = formantOf[(size_t) c];
        const auto fm = formants::at (vowel, k);
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
        const float formantOct = std::log2 (fm.hz) + driftOct + span * (spreadPos[(size_t) c] + jitterShare * tr.jitter);
        // Two resonant stages: a cell's -3 dB width is about 0.64 f / Q.
        const float formantQ = std::clamp (0.64f * fm.hz / bw, minQ, maxQ);
        // Louder formants have more cells; share the formant's level between them.
        const float formantDb = fm.gainDb - 10.0f * std::log10 ((float) cellsInFormant[(size_t) k]);

        const float freeOct = freeLowOct + freeSpanOct * tr.freePos;
        const float freeQ = std::clamp (4.0f / bwScale * std::exp2 (tr.freeQ), minQ, maxQ);

        // Width detunes L/R by up to 0.1 octave, but no further than a third of the formant's width.
        const float formantWidthOct = std::min (maxWidthOct, bwOct / 3.0f);

        const float s = std::clamp (scatter, 0.0f, 1.0f);
        return { formantOct + (freeOct - formantOct) * s - sizeOct,
                 std::exp2 (std::log2 (formantQ) + (std::log2 (freeQ) - std::log2 (formantQ)) * s),
                 formantDb + (tr.freeGainDb - formantDb) * s,
                 formantWidthOct + (maxWidthOct - formantWidthOct) * s };
    }

    void controlTick (int sampleOffset) noexcept
    {
        smoothTowardsTarget();
        updateAutoGain();

        mod.tickSources (tickDt, sampleOffset);
        mod.computeSlots();
        const FieldSettings e = effectiveSettings();

        // Life (phase 2): the chaos moves the throat. Flux × Wander sets how far; 0 = still.
        const auto& life = chaos.advance (e.driftHz, tickDt);
        const float roam = std::clamp (e.fluxAmount * e.wander, 0.0f, 1.0f);
        const float vowelNow = cur.vowel + roam * maxVowelWander * life.vowel;
        const float sizeNow = e.sizeOct + roam * maxSizeWanderOct * life.size;
        openness = roam * life.openness;
        for (size_t k = 0; k < formantDrift.size(); ++k)
            formantDrift[k] = roam * maxFormantWanderOct * life.formant[k];
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

        // LFO connections with a Flux share travel through the cells.
        mod.propagate (e.propagation, activeCells);

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
            // Level and the unmodulated position come from the knobs alone (shared by both channels).
            const auto base = place (c, vowelNow, sizeNow, bandwidthScale (cur.resonance), cur.scatter);
            const float f = std::exp2 (base.octs);
            cellFreq[(size_t) c] = f;
            // Cells pushed above the usable range fade out instead of piling up at Nyquist.
            targetGain[(size_t) c] = f < maxHz ? std::pow (10.0f, base.gainDb / 20.0f) : 0.0f;

            // Width: alternate cells lean left/right, and L/R cutoffs move apart slightly.
            const float side = (c % 2 == 0) ? 1.0f : -1.0f;
            const float detuneOct = base.widthOct * e.width * side;
            const float p = e.width * side;                // -1..1, +1 = left
            pan[0][(size_t) c] = std::min (1.0f, 1.0f + p);
            pan[1][(size_t) c] = std::min (1.0f, 1.0f - p);
            for (int ch = 0; ch < maxChannels; ++ch)
                levelMod[(size_t) ch][(size_t) c] = 1.0f;

            if (! updateCoefficients)
                continue;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                // Per-cell modulation (mono uses channel 0's for both; stereo moves L and R apart).
                const float vowelOff = mod.cell (ModTarget::Vowel, c, ch);
                const float resOff = mod.cell (ModTarget::Resonance, c, ch);
                const float scatterOff = mod.cell (ModTarget::Scatter, c, ch);
                const auto pl = (vowelOff == 0.0f && resOff == 0.0f && scatterOff == 0.0f)
                    ? base
                    : place (c, vowelNow + vowelRange * vowelOff, sizeNow,
                             bandwidthScale (cur.resonance + resOff), cur.scatter + scatterOff);

                float octs = pl.octs + (ch == 0 ? detuneOct : -detuneOct);
                if (broken)
                    octs = std::round (octs * 4.0f) * 0.25f;   // cutoffs snap to a quarter-octave grid
                const float chHz = std::clamp (std::exp2 (octs), 15.0f, maxHz);
                cellFreqNow[(size_t) ch][(size_t) c] = chHz;
                cellQ[(size_t) ch][(size_t) c] = pl.q;

                auto& cell = cells[(size_t) ch][(size_t) c];
                cell.setCoefficients (chHz, pl.q, sampleRate);
                // The band state grows with Q (unity peak output = state / Q), so the limit scales
                // with Q too: saturation then starts at the same output level at any resonance.
                cell.setSaturation (satLimit * pl.q, satAsym);
            }
        }

        for (int c = 0; c < activeCells; ++c)
            cellGain[(size_t) c] += (targetGain[(size_t) c] - cellGain[(size_t) c]) * smoothCoef;

        // Level: a filter must never amplify. Find the field's loudest point (every cell centre plus
        // one octave beyond each end, summing all cells as magnitudes) and scale it to unity.
        // Recomputed every 4th tick; drops at once, recovers smoothly.
        if (updateCoefficients && (normCounter++ % 4) == 0)
        {
            float loudest = 1.0e-6f;
            auto probe = [&] (float hz) {
                float sum = 0.0f;
                for (int c = 0; c < activeCells; ++c)
                    sum += cellGain[(size_t) c] * FluxCell::magnitude (hz / cellFreqNow[0][(size_t) c], cellQ[0][(size_t) c], 1.0f);
                loudest = std::max (loudest, sum);
            };
            float lowest = maxHz, highest = 0.0f;
            for (int c = 0; c < activeCells; ++c)
            {
                const float f = cellFreqNow[0][(size_t) c];
                probe (f);
                lowest = std::min (lowest, f);
                highest = std::max (highest, f);
            }
            probe (lowest * 0.5f);
            probe (std::min (highest * 2.0f, maxHz));
            const float unityNorm = 1.0f / loudest;
            fieldNorm = unityNorm < fieldNorm ? unityNorm : fieldNorm + (unityNorm - fieldNorm) * 0.2f;
        }

        // Makeup: narrow formants pass little of a broadband sound. Estimate how much of a pink input
        // (equal energy per octave, 20 Hz–20 kHz) gets through and bring that up to 3 dB under the
        // input, by at most +12 dB, so a tone on a formant peak is never more than 12 dB louder than
        // it came in (Auto Gain and the limiter catch the rest). Applied after the feedback loop.
        if (updateCoefficients && (makeupCounter++ % 16) == 0)
        {
            float energy = 0.0f;
            for (int i = 0; i < makeupPoints; ++i)
            {
                const float hz = 20.0f * std::exp2 ((float) i / makeupPointsPerOct);
                if (hz >= maxHz) continue;
                // Complex sum: neighbouring formants are out of phase between their peaks and
                // partly cancel there (a magnitude sum would over-estimate what gets through).
                std::complex<float> sum {};
                for (int c = 0; c < activeCells; ++c)
                {
                    // Two bandpass stages at the same Q: (j k x / (1 - x² + j k x))²
                    const float x = hz / cellFreqNow[0][(size_t) c], k = 1.0f / cellQ[0][(size_t) c];
                    const std::complex<float> stage = std::complex<float> (0.0f, k * x) / std::complex<float> (1.0f - x * x, k * x);
                    sum += cellGain[(size_t) c] * stage * stage;
                }
                energy += std::norm (sum);
            }
            const float passedDb = 10.0f * std::log10 (std::max (1.0e-12f, fieldNorm * fieldNorm * energy / (float) makeupPoints));
            makeupTarget = std::pow (10.0f, std::clamp (makeupTargetDb - passedDb, 0.0f, maxMakeupDb) / 20.0f);
        }
        makeup += (makeupTarget - makeup) * smoothCoef;

        // Feedback goes above unity at the top so the field can sing on its own;
        // the saturation in the loop and in the cells keeps that bounded.
        fbGain = 1.35f * e.feedback;   // the field peaks just under unity, so the top of the range self-oscillates
        const float dampHz = std::min (20000.0f * std::exp2 (-e.damping * 5.3f), maxHz);
        dampCoef = 1.0f - std::exp (-2.0f * 3.14159265f * dampHz / sampleRate);

        publishSnapshot();

        driveGain = std::pow (10.0f, e.driveDb / 20.0f);
        driveMakeup = 1.0f / std::sqrt (driveGain);
        driveBlend = std::min (1.0f, e.driveDb / 6.0f);
        outGain = std::pow (10.0f, e.outputDb / 20.0f);
    }

    float sampleRate = 44100.0f;
    float smoothCoef = 1.0f, agSmooth = 1.0f, tickDt = 0.001f;
    float envCoef = 0.001f, dcCoef = 0.999f;
    bool  firstTick = true;
    bool  stereo = true;
    int   samplesUntilTick = 0;
    uint32_t tickCounter = 0;

    FieldSettings target, cur;
    Matrix mod;
    float effMix = 1.0f;
    std::array<float, maxChannels> blockPeakPre {}, blockPeakPost {};

    int activeCells = 0;
    std::array<std::array<FluxCell, maxCells>, maxChannels> cells {};
    std::array<float, maxCells> cellFreq {}, cellGain {}, targetGain {};
    std::array<std::array<float, maxCells>, maxChannels> pan {}, cellFreqNow {};
    std::array<std::array<float, maxCells>, maxChannels> levelMod {}, cellQ {};
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
    static constexpr float maxSizeWanderOct = 0.25f;
    static constexpr float maxFormantWanderOct = 1.0f;   // each formant on its own
    static constexpr float opennessF1Oct = 0.4f, opennessOct = 0.12f;   // jaw: F1 moves most
    FluxChaos chaos;
    float openness = 0.0f;
    std::array<float, formants::numFormants> formantDrift {};
    static constexpr int makeupPointsPerOct = 6, makeupPoints = 10 * makeupPointsPerOct + 1;   // 20 Hz … 20.5 kHz
    static constexpr float makeupTargetDb = -3.0f, maxMakeupDb = 12.0f;
    float makeup = 1.0f, makeupTarget = 1.0f;
    uint32_t makeupCounter = 0;
    float driveGain = 1.0f, driveMakeup = 1.0f, driveBlend = 0.0f, outGain = 1.0f;

    // Feedback loop
    std::array<float, maxChannels> fbState {}, dampState {}, dcX {}, dcY {};
    float fbGain = 0.0f, dampCoef = 1.0f;

    // Auto-gain
    float inPow = 0.0f, wetPow = 0.0f, agGain = 1.0f;
};

} // namespace flux

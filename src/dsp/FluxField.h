#pragma once

#include "FluxCell.h"
#include "FluxChaos.h"
#include "FluxPropagator.h"
#include "FormantTable.h"
#include "PitchAnchor.h"
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
    float coupling   = 0.3f;    // how much ringing cells feed their neighbours (times Flux)
    float feed       = 0.5f;    // life cycle driver: 0 = chance, 1 = input energy
    float lifespanSec = 2.0f;   // mean life of a cell (chance) / how long it survives starving (energy)
    float rasp       = 0.0f;    // throat roughness: jitter + sub-octave growl (times Flux); 0 = clean
    bool  stereoIndependent = false;   // each channel has its own chaos, population and rasp

    // Pitch anchor: cells snap to notes of a key/scale or to harmonics of the input's pitch.
    AnchorMode  anchor = AnchorMode::Off;
    int         anchorKey = 0;          // 0 = C … 11 = B
    AnchorScale anchorScale = AnchorScale::Major;
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
        energyCoef   = tickCoef (0.02f);
        excAttack    = tickCoef (0.08f);
        excRelease   = tickCoef (1.5f);
        resetLife();
        resetRasp();
        dcCoef       = 1.0f - 2.0f * 3.14159265f * 10.0f / sampleRate; // DC blocker at ~10 Hz

        mod.prepare (sampleRate, ticksPerSecond);
        for (uint32_t p = 0; p < maxChannels; ++p) chaos[p].reset (p);
        pitch.prepare (sampleRate);
        pitchTicks = std::max (1, (int) std::lround (0.02f * ticksPerSecond));   // analyse every ~20 ms
        anchorGlide = tickCoef (0.008f);
        anchorActive = false;
        agGain = 1.0f;
        for (auto& ch : levelMod) ch.fill (1.0f);
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

            if (cur.anchor == AnchorMode::Follow)   // the detector only listens in Follow mode
                pitch.push (numCh == 2 ? 0.5f * (left[i] + right[i]) : left[i]);

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
    float getCellFrequency (int i) const noexcept { return cellFreq[(size_t) i]; }       // before modulation and width
    float getCellFrequencyNow (int i, int ch = 0) const noexcept { return cellFreqNow[(size_t) ch][(size_t) i]; }
    float getCellQ (int i, int ch = 0) const noexcept { return cellQ[(size_t) ch][(size_t) i]; }
    float getCellGain (int i) const noexcept { return cellGain[(size_t) i]; }
    int   getCellFormant (int i) const noexcept { return formantOf[(size_t) i]; }
    int   getActiveCells() const noexcept { return activeCells; }
    float getCellExcitation (int i) const noexcept { return excitation[(size_t) i]; }
    float getCellVitality (int i, int ch = 0) const noexcept { return life[(size_t) ch][(size_t) i].vitality; }
    bool  isCellAlive (int i, int ch = 0) const noexcept { return life[(size_t) ch][(size_t) i].alive; }
    float getPitchHz() const noexcept { return pitch.getPitch(); }
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
            sum += heard * cellGain[ci] * levelMod[(size_t) ch][ci] * (stereo ? pan[(size_t) ch][ci] : 1.0f);
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
        glide (cur.sizeOct, target.sizeOct);
        glide (cur.resonance, target.resonance);
        glide (cur.scatter, target.scatter);
        glide (cur.width, target.width);
        glide (cur.feedback, target.feedback);
        glide (cur.damping, target.damping);
        glide (cur.fluxAmount, target.fluxAmount);
        glide (cur.wander, target.wander);
        glide (cur.feed, target.feed);
        cur.lifespanSec = target.lifespanSec;
        cur.stereoIndependent = target.stereoIndependent;
        cur.anchor = target.anchor;
        cur.anchorKey = target.anchorKey;
        cur.anchorScale = target.anchorScale;
        glide (cur.coupling, target.coupling);
        cur.driftHz = target.driftHz;
        glide (cur.propagation.travelSec, target.propagation.travelSec);
        glide (cur.propagation.viscosity, target.propagation.viscosity);
        glide (cur.propagation.wavelength, target.propagation.wavelength);
        glide (cur.propagation.turbulence, target.propagation.turbulence);
        glide (cur.rasp, target.rasp);
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
        e.coupling  = m (FP::Coupling, cur.coupling, ModTarget::Coupling);
        e.feed      = m (FP::Feed, cur.feed, ModTarget::Feed);
        e.lifespanSec = m (FP::Lifespan, cur.lifespanSec, ModTarget::Lifespan);
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

        // Bleed neighbours: the cells next to each other in the field; across a formant border at half weight.
        for (int i = 0; i < n; ++i)
        {
            const auto ci = (size_t) i;
            bleedLeft[ci]  = i > 0     ? (formantOf[ci - 1] == formantOf[ci] ? 1.0f : 0.5f) : 0.0f;
            bleedRight[ci] = i + 1 < n ? (formantOf[ci + 1] == formantOf[ci] ? 1.0f : 0.5f) : 0.0f;
        }
    }

    struct Placement { float octs, q, gainDb, widthOct; };   // widthOct: how far Width may detune L/R

    // Where cell c sits for a vowel position, size, bandwidth factor and scatter amount.
    // pop: whose drift and life to use (the channel's own when Stereo life is independent, else 0).
    Placement place (int c, int pop, float vowel, float sizeOct, float bwScale, float scatter) const noexcept
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
        const float driftOct = formantDrift[(size_t) pop][(size_t) k] + openness[(size_t) pop] * (k == 0 ? opennessF1Oct : opennessOct);
        // Life: a reborn cell lands a little elsewhere in its formant (up to its whole span, times Flux).
        const auto& lf = life[(size_t) pop][(size_t) c];
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

        // Width detunes L/R by up to 0.1 octave, but no further than a third of the formant's width.
        const float formantWidthOct = std::min (maxWidthOct, bwOct / 3.0f);

        const float s = std::clamp (scatter, 0.0f, 1.0f);
        return { formantOct + (freeOct - formantOct) * s - sizeOct,
                 std::exp2 (std::log2 (formantQ) + (std::log2 (freeQ) - std::log2 (formantQ)) * s),
                 formantDb + (tr.freeGainDb - formantDb) * s,
                 formantWidthOct + (maxWidthOct - formantWidthOct) * s };
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
        for (size_t p = 0; p < life.size(); ++p)
        {
            auto& rng = lifeRng[p];
            rng.seed (0xC0FFEE11u + 0x2545F491u * (uint32_t) p);
            for (auto& l : life[p])
            {
                l = CellLife {};
                rollLife (l, rng);
                l.shift = l.freeShift = 0.0f;                    // the first generation sits on the knobs' layout
                l.hunger = 0.5f * (rng.nextBipolar() + 1.0f);    // staggered, so they don't all die together
            }
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

        for (int p = 0; p < numPopulations; ++p)
        for (int c = 0; c < activeCells; ++c)
        {
            auto& l = life[(size_t) p][(size_t) c];
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
                    rollLife (l, lifeRng[(size_t) p]);   // silent now: move it to where it will be reborn
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
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int c = 0; c < maxCells; ++c)
            {
                const auto seed = 0xA5A5F00Du + 131u * (uint32_t) c + 7919u * (uint32_t) ch;
                raspFreqNoise[(size_t) ch][(size_t) c].seed (seed);
                raspAmpNoise[(size_t) ch][(size_t) c].seed (seed ^ 0x5bd1e995u);
                raspState[(size_t) ch][(size_t) c] = {};
                raspOct[(size_t) ch][(size_t) c] = 0.0f;
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

        for (int ch = 0; ch < maxChannels; ++ch)
            for (int c = 0; c < activeCells; ++c)
            {
                auto& r = raspState[(size_t) ch][(size_t) c];
                if (raspOn && ch == 1 && numPopulations == 1)   // linked: the right channel shakes with the left
                {
                    if (! wasOn) r = {};
                    raspOct[1][(size_t) c] = raspOct[0][(size_t) c];
                    r.ampStep = (raspState[0][(size_t) c].amp + raspState[0][(size_t) c].ampStep * (float) controlInterval - r.amp) / (float) controlInterval;
                    continue;
                }
                if (! raspOn)
                {
                    raspOct[(size_t) ch][(size_t) c] = 0.0f;
                    r = {};
                    continue;
                }
                if (! wasOn) r = {};
                raspOct[(size_t) ch][(size_t) c] = amount * maxRaspOct * raspFreqNoise[(size_t) ch][(size_t) c].advance (raspRateHz, tickDt);
                const float targetAmp = 1.0f + amount * maxRaspAmp * raspAmpNoise[(size_t) ch][(size_t) c].advance (raspRateHz, tickDt);
                r.ampStep = (targetAmp - r.amp) / (float) controlInterval;
            }
    }

    void controlTick (int sampleOffset) noexcept
    {
        smoothTowardsTarget();
        updateAutoGain();

        mod.tickSources (tickDt, sampleOffset);
        mod.computeSlots();
        const FieldSettings e = effectiveSettings();

        // Stereo life: one population / chaos / rasp for both channels, or one per channel.
        numPopulations = (stereo && cur.stereoIndependent) ? 2 : 1;

        // Life (phase 2): the chaos moves the throat. Flux × Wander sets how far; 0 = still.
        const float roam = std::clamp (e.fluxAmount * e.wander, 0.0f, 1.0f);
        std::array<float, maxChannels> vowelNow {}, sizeNow {};
        for (int p = 0; p < maxChannels; ++p)
        {
            const auto& chaosOut = chaos[(size_t) p].advance (e.driftHz, tickDt);   // both run, so switching is seamless
            vowelNow[(size_t) p] = cur.vowel + roam * maxVowelWander * chaosOut.vowel;
            sizeNow[(size_t) p] = e.sizeOct + roam * maxSizeWanderOct * chaosOut.size;
            openness[(size_t) p] = roam * chaosOut.openness;
            for (size_t k = 0; k < formants::numFormants; ++k)
                formantDrift[(size_t) p][k] = roam * maxFormantWanderOct * chaosOut.formant[k];
        }

        // Life (phase 3): energy coupling. Flux × Coupling sets how strongly.
        updateCoupling (std::clamp (e.fluxAmount * e.coupling, 0.0f, 1.0f));

        // Life (phase 4): cells are born, live, die and are reborn. Flux sets how deep it cuts.
        updateLife (std::clamp (e.fluxAmount, 0.0f, 1.0f), std::clamp (e.feed, 0.0f, 1.0f), std::max (0.01f, e.lifespanSec));

        // Life (phase 5): rasp. Flux × Rasp sets how rough.
        updateRasp (std::clamp (e.fluxAmount * e.rasp, 0.0f, 1.0f));
        effMix = e.mix;

        // Pitch anchor (phase 6): Follow listens to the input every ~20 ms.
        const bool anchorOn = cur.anchor != AnchorMode::Off;
        if (cur.anchor == AnchorMode::Follow && ++pitchCounter >= pitchTicks)
        {
            pitchCounter = 0;
            pitch.analyse();
        }
        const float f0 = pitch.getPitch();
        const float f0Oct = f0 > 0.0f ? std::log2 (f0) : 0.0f;
        auto snap = [&] (float oct) {
            if (cur.anchor == AnchorMode::Scale) return anchor::snapToScale (oct, cur.anchorKey, cur.anchorScale);
            if (cur.anchor == AnchorMode::Follow && f0 > 0.0f) return anchor::snapToHarmonic (oct, f0Oct);
            return oct;
        };

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
            const auto base = place (c, 0, vowelNow[0], sizeNow[0], bandwidthScale (cur.resonance), cur.scatter);
            const float f = std::exp2 (base.octs);
            cellFreq[(size_t) c] = f;
            // Cells pushed above the usable range fade out instead of piling up at Nyquist.
            targetGain[(size_t) c] = f < maxHz ? std::pow (10.0f, base.gainDb / 20.0f) * excitedGain[(size_t) c] : 0.0f;

            // Width: alternate cells lean left/right, and L/R cutoffs move apart slightly.
            const float side = (c % 2 == 0) ? 1.0f : -1.0f;
            const float detuneOct = base.widthOct * e.width * side;
            const float p = e.width * side;                // -1..1, +1 = left
            pan[0][(size_t) c] = std::min (1.0f, 1.0f + p);
            pan[1][(size_t) c] = std::min (1.0f, 1.0f - p);
            for (int ch = 0; ch < maxChannels; ++ch)
                levelMod[(size_t) ch][(size_t) c] = 1.0f - lifeDepth * (1.0f - life[(size_t) (ch < numPopulations ? ch : 0)][(size_t) c].vitality);

            if (! updateCoefficients)
                continue;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                // Per-cell modulation (mono uses channel 0's for both; stereo moves L and R apart).
                const float vowelOff = mod.cell (ModTarget::Vowel, c, ch);
                const float resOff = mod.cell (ModTarget::Resonance, c, ch);
                const float scatterOff = mod.cell (ModTarget::Scatter, c, ch);
                const int pop = ch < numPopulations ? ch : 0;
                const auto pl = (pop == 0 && vowelOff == 0.0f && resOff == 0.0f && scatterOff == 0.0f)
                    ? base
                    : place (c, pop, vowelNow[(size_t) pop] + vowelRange * vowelOff, sizeNow[(size_t) pop],
                             bandwidthScale (cur.resonance + resOff), cur.scatter + scatterOff);

                float octs = pl.octs + (ch == 0 ? detuneOct : -detuneOct);
                if (anchorOn)
                {
                    // Snap onto the nearest note / harmonic and glide there (~8 ms), so cells moving
                    // between notes step without clicks. Rasp still shakes them afterwards.
                    auto& a = anchorOct[(size_t) ch][(size_t) c];
                    const float targetOct = snap (octs);
                    a = anchorActive ? a + (targetOct - a) * anchorGlide : targetOct;
                    octs = a;
                }
                else if (broken)
                {
                    octs = std::round (octs * 4.0f) * 0.25f;   // cutoffs snap to a quarter-octave grid
                }
                octs += raspOct[(size_t) ch][(size_t) c];
                const float chHz = std::clamp (std::exp2 (octs), 15.0f, maxHz);
                cellFreqNow[(size_t) ch][(size_t) c] = chHz;
                const float q = std::min (maxQ, pl.q * excitedQ[(size_t) c]);
                cellQ[(size_t) ch][(size_t) c] = q;

                auto& cell = cells[(size_t) ch][(size_t) c];
                cell.setCoefficients (chHz, q, sampleRate);
                // The band state grows with Q (unity peak output = state / Q), so the limit scales
                // with Q too: saturation then starts at the same output level at any resonance.
                cell.setSaturation (satLimit * q, satAsym);
            }
        }

        for (int c = 0; c < activeCells; ++c)
            cellGain[(size_t) c] += (targetGain[(size_t) c] - cellGain[(size_t) c]) * smoothCoef;
        if (updateCoefficients)
            anchorActive = anchorOn;   // the next switch-on jumps straight onto the notes

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
                    const float m = cellGain[(size_t) c] * FluxCell::magnitude (hz / cellFreqNow[0][(size_t) c], cellQ[0][(size_t) c], 1.0f);
                    sum += m * levelMod[0][(size_t) c];
                    full += m;
                }
                loudest = std::max (loudest, sum);
                loudestFull = std::max (loudestFull, full);
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
                std::complex<float> sum {};
                for (int c = 0; c < activeCells; ++c)
                {
                    // Two bandpass stages at the same Q: (j k x / (1 - x² + j k x))²
                    const float x = hz / cellFreqNow[0][(size_t) c], k = 1.0f / cellQ[0][(size_t) c];
                    const std::complex<float> stage = std::complex<float> (0.0f, k * x) / std::complex<float> (1.0f - x * x, k * x);
                    sum += cellGain[(size_t) c] * levelMod[0][(size_t) c] * stage * stage;   // living cells only
                }
                pinkEnergy += std::norm (sum);
            }
            const float passedDb = 10.0f * std::log10 (std::max (1.0e-12f, fieldNorm * fieldNorm * pinkEnergy / (float) makeupPoints));
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
    std::array<FluxChaos, maxChannels> chaos;   // [1] only heard when Stereo life is independent
    std::array<float, maxChannels> openness {};
    std::array<std::array<float, formants::numFormants>, maxChannels> formantDrift {};
    int numPopulations = 1;

    // Pitch anchor
    PitchDetector pitch;
    int pitchTicks = 30, pitchCounter = 0;
    float anchorGlide = 1.0f;
    bool anchorActive = false;
    std::array<std::array<float, maxCells>, maxChannels> anchorOct {};

    // Life: cycle
    static constexpr float maxPopulationLift = 4.0f;   // +12 dB: how far the norm lifts a thinned-out population
    static constexpr float minFood = 1.0e-7f;          // energy below this (about -70 dB) is no food
    static constexpr float minRebirthSec = 0.05f;      // energy-driven rebirth takes at least this long
    std::array<std::array<CellLife, maxCells>, maxChannels> life {};   // [1]: the right channel's own population

    // Life: rasp
    static constexpr float maxRaspOct = 0.08f, maxRaspAmp = 0.5f, maxGrowl = 0.8f, raspRateHz = 180.0f;
    std::array<std::array<SmoothNoise, maxCells>, maxChannels> raspFreqNoise, raspAmpNoise;
    std::array<std::array<RaspState, maxCells>, maxChannels> raspState {};
    std::array<std::array<float, maxCells>, maxChannels> raspOct {};
    bool  raspOn = false;
    float growlHigh = 1.0f, growlLow = 1.0f;
    std::array<FastRandom, maxChannels> lifeRng;
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
    float fbGain = 0.0f, dampCoef = 1.0f;

    // Auto-gain
    float inPow = 0.0f, wetPow = 0.0f, agGain = 1.0f;
};

} // namespace flux

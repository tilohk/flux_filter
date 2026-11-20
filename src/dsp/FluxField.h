#pragma once

#include "FluxCell.h"
#include "FluxLfo.h"
#include "FluxPropagator.h"
#include "mod/ModMatrix.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace flux
{

enum class CellScale { Log = 0, Harmonic, Bell, Pentatonic };
enum class Character { Clean = 0, Warm, Broken };

// Everything that shapes the field. Plain values, filled from the plugin parameters each block.
struct FieldSettings
{
    int   density    = 12;      // number of active cells, 1..maxCells
    float centerHz   = 600.0f;  // Log: middle of the field. Other scales: lowest cell (root)
    float spread     = 4.0f;    // Log: span in octaves. Other scales: stretch, 4 = natural ratios
    CellScale scale  = CellScale::Log;
    float tiltDbOct  = 0.0f;    // gain tilt across the field, dB per octave away from center
    float resonance  = 0.6f;    // 0..1 → Q 0.5..64
    float morph      = 1.0f;    // 0 LP · 1 BP · 2 HP · 3 Notch
    float combine    = 0.0f;    // 0 = parallel bank, 1 = serial chain, between = blend
    float width      = 0.5f;    // stereo: L/R cutoff offset + alternating pan (parallel)

    // Organic layer
    float drift      = 0.15f;   // 0..1, slow independent wandering of every cell (up to ±1/4 octave)
    Character character = Character::Warm;
    float feedback   = 0.0f;    // 0..1, wet signal back into the field; near 1 it self-oscillates
    float damping    = 0.3f;    // 0..1, darkens the feedback path
    bool  autoGain   = true;    // keep the wet signal at the input's loudness
    bool  limiter    = true;    // soft safety ceiling at 0 dBFS

    // Flux: the built-in LFO and how its movement travels through the field
    float fluxRateHz = 0.3f;
    LfoShape fluxShape = LfoShape::Sine;
    float fluxDepth  = 0.5f;    // cutoff movement, ± octaves
    float fluxLevel  = 0.0f;    // 0..1, the wave also swells / ducks the cells' loudness
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

// A field of resonant cells spread across the spectrum.
// Coefficients are recomputed at control rate (every controlInterval samples) from smoothed
// settings, drift and the flux wave (FluxPropagator), which moves each cell's cutoff and level.
class FluxField
{
public:
    static constexpr int maxCells = 24;
    static constexpr int maxChannels = 2;
    static constexpr int controlInterval = 32;
    static constexpr float wetCeiling = 4.0f, invWetCeiling = 1.0f / wetCeiling;
    using Matrix = ModMatrix<maxCells, maxChannels>;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        const float ticksPerSecond = sampleRate / (float) controlInterval;
        auto tickCoef = [ticksPerSecond] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * ticksPerSecond)); };
        smoothCoef   = tickCoef (0.025f);
        driftSmooth  = tickCoef (0.12f);
        agSmooth     = tickCoef (0.25f);
        tickDt       = 1.0f / ticksPerSecond;
        envCoef      = 1.0f - std::exp (-1.0f / (0.3f * sampleRate));   // loudness detectors, per sample
        dcCoef       = 1.0f - 2.0f * 3.14159265f * 10.0f / sampleRate; // DC blocker at ~10 Hz

        rng.seed (0x1234567u);
        lfo.reset();
        propagator.prepare (ticksPerSecond);
        mod.prepare (sampleRate, ticksPerSecond);
        for (auto& ch : driftWalk)   ch.fill (0.0f);
        for (auto& ch : driftSmoothed) ch.fill (0.0f);
        agGain = 1.0f;
        for (auto& ch : levelMod) ch.fill (1.0f);
        reset();
        firstTick = true;
    }

    void reset() noexcept
    {
        for (auto& ch : parallelCells) for (auto& c : ch) c.reset();
        for (auto& ch : serialCells)   for (auto& c : ch) c.reset();
        fbState.fill (0.0f); dampState.fill (0.0f); dcX.fill (0.0f); dcY.fill (0.0f);
        inPow = wetPow = 0.0f;
        samplesUntilTick = 0;
    }

    void setSettings (const FieldSettings& s) noexcept { target = s; }

    // Modulation: settings per block (audio thread), transport per block, shapes from any thread.
    void setModSettings (const ModSettings& s) noexcept { mod.setSettings (s); }
    void setTransport (const Transport& t) noexcept { mod.setTransport (t); }
    void setCustomShape (int lfoIndex, const CurveShape& shape) { mod.setCustomShape (lfoIndex, shape); }

    // In-place processing. right may be nullptr for mono.
    void process (float* left, float* right, int numSamples) noexcept
    {
        const int numCh = right != nullptr ? 2 : 1;
        stereo = numCh == 2;
        float* chans[2] = { left, right };

        for (int i = 0; i < numSamples; ++i)
        {
            if (samplesUntilTick <= 0)
            {
                controlTick();
                samplesUntilTick = controlInterval;
            }
            --samplesUntilTick;

            float inSq = 0.0f, wetSq = 0.0f;
            float wetCh[2] = { 0.0f, 0.0f };
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = std::max (peak, std::abs (chans[ch][i]));
            mod.pushInput (peak);

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = chans[ch][i];
                const float wet = blockDc (processWet (applyDrive (dry) + feedbackSignal (ch), ch), ch);
                fbState[(size_t) ch] = wet;
                wetCh[ch] = wet;
                inSq += dry * dry;
                wetSq += wet * wet;
            }

            // Linked loudness detectors (keep the stereo image intact).
            inPow  += (inSq  / (float) numCh - inPow)  * envCoef;
            wetPow += (wetSq / (float) numCh - wetPow) * envCoef;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = chans[ch][i];
                // Soft wet ceiling at +12 dBFS (transparent below ~0 dBFS), even with the limiter off.
                const float wet = wetCeiling * softClip (wetCh[ch] * agGain * invWetCeiling);
                float out = (dry + (wet - dry) * effMix) * outGain;
                if (cur.limiter)
                    out = safetyLimit (out);

                if (! std::isfinite (out))   // last-resort guard: never pass NaN/inf to the host
                {
                    reset();
                    out = 0.0f;
                }
                chans[ch][i] = out;
            }
        }
    }

    // For tests / future visualiser (audio thread only).
    float getCellFrequency (int i) const noexcept { return cellFreq[(size_t) i]; }       // before drift
    float getCellFrequencyNow (int i, int ch = 0) const noexcept { return cellFreqNow[(size_t) ch][(size_t) i]; }
    int   getActiveCells() const noexcept { return activeCells; }
    float getAutoGain() const noexcept { return agGain; }
    float getFluxValue (int i, int ch = 0) const noexcept { return propagator.get (i, ch); }
    const Matrix& getMatrix() const noexcept { return mod; }

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
        float par = 0.0f, ser = 0.0f;

        if (parallelWeight > 0.0f)
        {
            for (int c = 0; c < activeCells; ++c)
                par += parallelCells[(size_t) ch][(size_t) c].process (x) * cellGain[(size_t) c]
                     * levelMod[(size_t) ch][(size_t) c]
                     * (stereo ? pan[(size_t) ch][(size_t) c] : 1.0f);
            par *= parallelNorm;
        }

        if (serialWeight > 0.0f)
        {
            ser = x;
            for (int c = 0; c < activeCells; ++c)
            {
                const float y = serialCells[(size_t) ch][(size_t) c].process (ser);
                ser += (y - ser) * serialDepth[(size_t) c] * levelMod[(size_t) ch][(size_t) c];
            }
        }

        return par * parallelWeight + ser * serialWeight;
    }

    static float cellRatio (CellScale scale, int i, float stretch) noexcept
    {
        switch (scale)
        {
            case CellScale::Harmonic:
                return std::pow ((float) (i + 1), stretch);
            case CellScale::Bell:
            {
                // Free-bar style inharmonic partials: 1, 2.78, 5.44, 9, 13.4, ...
                const float r = (2.0f * (float) i + 3.0f) / 3.0f;
                return std::pow (r * r, stretch);
            }
            case CellScale::Pentatonic:
            {
                static constexpr int degrees[5] = { 0, 3, 5, 7, 10 };
                const float semis = 12.0f * (float) (i / 5) + (float) degrees[i % 5];
                return std::exp2 (semis * stretch / 12.0f);
            }
            case CellScale::Log:
            default:
                return 1.0f; // handled separately
        }
    }

    void smoothTowardsTarget() noexcept
    {
        if (firstTick)
        {
            cur = target;
            logCenter = std::log2 (target.centerHz);
            firstTick = false;
            return;
        }
        auto glide = [this] (float& v, float t) { v += (t - v) * smoothCoef; };
        logCenter += (std::log2 (target.centerHz) - logCenter) * smoothCoef;
        cur.centerHz = std::exp2 (logCenter);
        glide (cur.spread, target.spread);
        glide (cur.tiltDbOct, target.tiltDbOct);
        glide (cur.resonance, target.resonance);
        glide (cur.morph, target.morph);
        glide (cur.combine, target.combine);
        glide (cur.width, target.width);
        glide (cur.drift, target.drift);
        glide (cur.feedback, target.feedback);
        glide (cur.damping, target.damping);
        glide (cur.fluxDepth, target.fluxDepth);
        glide (cur.fluxLevel, target.fluxLevel);
        glide (cur.propagation.travelSec, target.propagation.travelSec);
        glide (cur.propagation.viscosity, target.propagation.viscosity);
        glide (cur.propagation.wavelength, target.propagation.wavelength);
        glide (cur.propagation.turbulence, target.propagation.turbulence);
        glide (cur.propagation.stereo, target.propagation.stereo);
        cur.propagation.direction = target.propagation.direction;
        cur.fluxRateHz = target.fluxRateHz;
        cur.fluxShape = target.fluxShape;
        glide (cur.driveDb, target.driveDb);
        glide (cur.mix, target.mix);
        glide (cur.outputDb, target.outputDb);
        cur.scale = target.scale;
        cur.density = target.density;
        cur.character = target.character;
        cur.autoGain = target.autoGain;
        cur.limiter = target.limiter;
    }

    // Ornstein-Uhlenbeck walk per cell and channel: wanders, but always drifts back home.
    void updateDrift() noexcept
    {
        constexpr float theta = 2.0f * 3.14159265f * 0.25f;            // pull back, ~0.6 s
        const float sigma = std::sqrt (2.0f * theta) * std::sqrt (tickDt); // stationary σ = 1
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int c = 0; c < maxCells; ++c)
            {
                auto& w = driftWalk[(size_t) ch][(size_t) c];
                w += -theta * w * tickDt + sigma * rng.nextGaussianish();
                auto& s = driftSmoothed[(size_t) ch][(size_t) c];
                s += (w - s) * driftSmooth;
            }
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
            const float targetAg = std::clamp (std::sqrt (inPow / (wetPow + 1.0e-12f)), 0.125f, 8.0f); // ±18 dB
            agGain += (targetAg - agGain) * agSmooth;
        }
    }

    // Global parameters after modulation (natural units).
    FieldSettings effectiveSettings() const noexcept
    {
        using spec::FP;
        auto m = [this] (FP p, float base, ModTarget t) { return Matrix::modulated (p, base, mod.global (t)); };
        FieldSettings e = cur;
        e.spread    = m (FP::Spread, cur.spread, ModTarget::Spread);
        e.tiltDbOct = m (FP::Tilt, cur.tiltDbOct, ModTarget::Tilt);
        e.combine   = m (FP::Combine, cur.combine, ModTarget::Combine);
        e.width     = m (FP::Width, cur.width, ModTarget::Width);
        e.drift     = m (FP::Drift, cur.drift, ModTarget::Drift);
        e.feedback  = m (FP::Feedback, cur.feedback, ModTarget::Feedback);
        e.damping   = m (FP::Damping, cur.damping, ModTarget::Damping);
        e.fluxDepth = m (FP::FluxDepth, cur.fluxDepth, ModTarget::FluxDepth);
        e.fluxLevel = m (FP::FluxLevel, cur.fluxLevel, ModTarget::FluxLevel);
        e.propagation.travelSec  = m (FP::Travel, cur.propagation.travelSec, ModTarget::Travel);
        e.propagation.viscosity  = m (FP::Viscosity, cur.propagation.viscosity, ModTarget::Viscosity);
        e.propagation.wavelength = m (FP::Wavelength, cur.propagation.wavelength, ModTarget::Wavelength);
        e.propagation.turbulence = m (FP::Turbulence, cur.propagation.turbulence, ModTarget::Turbulence);
        e.propagation.stereo     = m (FP::FluxStereo, cur.propagation.stereo, ModTarget::FluxStereo);
        e.driveDb   = m (FP::Drive, cur.driveDb, ModTarget::Drive);
        e.mix       = m (FP::Mix, cur.mix, ModTarget::Mix);
        e.outputDb  = m (FP::Output, cur.outputDb, ModTarget::Output);
        return e;
    }

    static float qFromResonance (float r) noexcept { return 0.5f * std::exp2 (std::clamp (r, 0.0f, 1.0f) * 7.0f); }

    void controlTick() noexcept
    {
        smoothTowardsTarget();
        updateDrift();
        updateAutoGain();

        // Modulation: the flux LFO (rate uses last tick's offsets), then every source and the matrix.
        const float fluxRate = Matrix::modulated (spec::FP::FluxRate, cur.fluxRateHz, mod.global (ModTarget::FluxRate));
        const float fluxLfoValue = lfo.advance (fluxRate, cur.fluxShape, tickDt);
        mod.tickSources (tickDt, fluxLfoValue);
        mod.computeSlots();
        const FieldSettings e = effectiveSettings();
        effMix = e.mix;

        // Broken: coefficients only update every 8th tick, so movement steps instead of gliding.
        const bool broken = cur.character == Character::Broken;
        const bool updateCoefficients = ! broken || (tickCounter++ % 8) == 0;

        const int newCount = std::clamp (cur.density, 1, maxCells);
        for (int c = activeCells; c < newCount; ++c)   // newly woken cells start silent
        {
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                parallelCells[(size_t) ch][(size_t) c].reset();
                serialCells[(size_t) ch][(size_t) c].reset();
            }
            cellGain[(size_t) c] = 0.0f;
        }
        activeCells = newCount;

        // The waves: the flux LFO and every Flux-enabled matrix slot travel through the cells.
        propagator.process (fluxLfoValue, e.propagation, activeCells);
        mod.propagate (e.propagation, activeCells);

        const auto& centerSpec = spec::get (spec::FP::Center);
        const float centerNorm = centerSpec.to01 (cur.centerHz);
        const float maxHz = 0.45f * sampleRate;
        const float stretch = e.spread / 4.0f;
        const float serialBase = std::min (1.0f, 3.0f / (float) activeCells);
        const float driftOct = 0.25f * e.drift;
        const float baseQ = qFromResonance (cur.resonance);   // static compensation; auto gain does the rest

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
            float f;
            if (cur.scale == CellScale::Log)
            {
                const float pos = activeCells > 1 ? (float) c / (float) (activeCells - 1) - 0.5f : 0.0f;
                f = cur.centerHz * std::exp2 (pos * e.spread);
            }
            else
            {
                f = cur.centerHz * cellRatio (cur.scale, c, stretch);
            }
            cellFreq[(size_t) c] = f;

            // Cells pushed above the usable range fade out instead of piling up at Nyquist.
            const float audible = f < maxHz ? 1.0f : 0.0f;
            const float octFromCenter = std::log2 (f / cur.centerHz);
            const float tiltDb = std::clamp (e.tiltDbOct * octFromCenter, -24.0f, 24.0f);
            targetGain[(size_t) c] = audible * std::pow (10.0f, tiltDb / 20.0f);

            // Width: alternate cells lean left/right, and L/R cutoffs drift apart slightly.
            const float side = (c % 2 == 0) ? 1.0f : -1.0f;
            const float detuneOct = 0.1f * e.width * side;
            const float p = e.width * side;                // -1..1, +1 = left
            pan[0][(size_t) c] = std::min (1.0f, 1.0f + p);
            pan[1][(size_t) c] = std::min (1.0f, 1.0f - p);

            // Level follows the waves smoothly, even in Broken (only cutoffs step there).
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                const float v = std::clamp (propagator.get (c, ch), -1.0f, 1.0f);
                const float swell = 1.0f + e.fluxLevel * (v - 1.0f) * 0.5f;
                const float matrixLevel = std::clamp (1.0f + mod.cell (ModTarget::CellLevel, c, ch), 0.0f, 2.0f);
                levelMod[(size_t) ch][(size_t) c] = swell * matrixLevel;
            }

            if (! updateCoefficients)
                continue;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                // Mono uses channel 0's drift and waves for both; stereo moves L and R independently.
                const float d = driftSmoothed[(size_t) ch][(size_t) c] * driftOct;
                const float wave = propagator.get (c, ch) * e.fluxDepth;

                // Matrix cutoff modulation lives in the Center knob's range: the same offset moves a
                // cell as far as turning Center would.
                const float cutOff = mod.cell (ModTarget::Cutoff, c, ch);
                const float matrixOct = cutOff != 0.0f
                    ? std::log2 (centerSpec.from01 (centerNorm + cutOff) / cur.centerHz) : 0.0f;

                float octs = std::log2 (f) + (ch == 0 ? detuneOct : -detuneOct) + d + wave + matrixOct;
                if (broken)
                    octs = std::round (octs * 4.0f) * 0.25f;   // cutoffs snap to a quarter-octave grid
                const float chHz = std::clamp (std::exp2 (octs), 15.0f, maxHz);
                cellFreqNow[(size_t) ch][(size_t) c] = chHz;

                const float q = qFromResonance (cur.resonance + mod.cell (ModTarget::Resonance, c, ch));
                const float morph = std::clamp (cur.morph + 3.0f * mod.cell (ModTarget::Morph, c, ch), 0.0f, 3.0f);

                auto& pc = parallelCells[(size_t) ch][(size_t) c];
                auto& sc = serialCells[(size_t) ch][(size_t) c];
                pc.setCoefficients (chHz, q, sampleRate);
                sc.setCoefficients (chHz, q, sampleRate);
                pc.setMorph (morph);
                sc.setMorph (morph);
                // The band state grows with Q (unity peak output = state / Q), so the limit scales
                // with Q too: saturation then starts at the same output level at any resonance.
                pc.setSaturation (satLimit * q, satAsym);
                sc.setSaturation (satLimit * q, satAsym);
            }
        }

        // Tilt redistributes energy instead of adding it: normalise gains to unit mean-square.
        float sumSq = 0.0f;
        for (int c = 0; c < activeCells; ++c) sumSq += targetGain[(size_t) c] * targetGain[(size_t) c];
        const float tiltNorm = sumSq > 1.0e-9f ? std::sqrt ((float) activeCells / sumSq) : 0.0f;
        for (int c = 0; c < activeCells; ++c)
        {
            auto& g = cellGain[(size_t) c];
            g += (targetGain[(size_t) c] * tiltNorm - g) * smoothCoef;
            serialDepth[(size_t) c] = std::clamp (serialBase * g, 0.0f, 1.0f);
        }

        // Static level compensation: narrow (high-Q) bands pass less broadband energy, many cells
        // add up. Auto-gain does the rest adaptively.
        parallelNorm = 1.5f * std::pow (baseQ, 0.3f) / std::sqrt ((float) activeCells);

        const float prevSerial = serialWeight;
        serialWeight = std::clamp (e.combine, 0.0f, 1.0f);
        parallelWeight = 1.0f - serialWeight;
        if (prevSerial <= 0.0f && serialWeight > 0.0f)
            for (auto& ch : serialCells) for (auto& c : ch) c.reset();

        // Feedback goes slightly above unity at the top so the field can sing on its own;
        // the saturation in the loop and in the cells keeps that bounded.
        fbGain = 1.1f * e.feedback;
        const float dampHz = std::min (20000.0f * std::exp2 (-e.damping * 5.3f), maxHz);
        dampCoef = 1.0f - std::exp (-2.0f * 3.14159265f * dampHz / sampleRate);

        driveGain = std::pow (10.0f, e.driveDb / 20.0f);
        driveMakeup = 1.0f / std::sqrt (driveGain);
        driveBlend = std::min (1.0f, e.driveDb / 6.0f);
        outGain = std::pow (10.0f, e.outputDb / 20.0f);
    }

    float sampleRate = 44100.0f;
    float smoothCoef = 1.0f, driftSmooth = 1.0f, agSmooth = 1.0f, tickDt = 0.001f;
    float envCoef = 0.001f, dcCoef = 0.999f;
    bool  firstTick = true;
    bool  stereo = true;
    int   samplesUntilTick = 0;
    uint32_t tickCounter = 0;

    FieldSettings target, cur;
    float logCenter = 0.0f;
    FastRandom rng;
    FluxLfo lfo;
    FluxPropagator<maxCells, maxChannels> propagator;
    Matrix mod;
    float effMix = 1.0f;

    int activeCells = 0;
    std::array<std::array<FluxCell, maxCells>, maxChannels> parallelCells {}, serialCells {};
    std::array<float, maxCells> cellFreq {}, cellGain {}, targetGain {}, serialDepth {};
    std::array<std::array<float, maxCells>, maxChannels> pan {}, driftWalk {}, driftSmoothed {}, cellFreqNow {};
    std::array<std::array<float, maxCells>, maxChannels> levelMod {};

    float parallelNorm = 1.0f, parallelWeight = 1.0f, serialWeight = 0.0f;
    float driveGain = 1.0f, driveMakeup = 1.0f, driveBlend = 0.0f, outGain = 1.0f;

    // Feedback loop
    std::array<float, maxChannels> fbState {}, dampState {}, dcX {}, dcY {};
    float fbGain = 0.0f, dampCoef = 1.0f;

    // Auto-gain
    float inPow = 0.0f, wetPow = 0.0f, agGain = 1.0f;
};

} // namespace flux

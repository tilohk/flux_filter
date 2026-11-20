#pragma once

#include "FluxCell.h"

#include <array>
#include <cmath>

namespace flux
{

enum class CellScale { Log = 0, Harmonic, Bell, Pentatonic };

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
    float driveDb    = 0.0f;    // input saturation
    float mix        = 1.0f;    // dry/wet
    float outputDb   = 0.0f;
};

// A field of resonant cells spread across the spectrum.
// Coefficients are recomputed at control rate (every controlInterval samples) from smoothed
// settings; that is also where the per-cell "flux" propagation will hook in (phase 3).
class FluxField
{
public:
    static constexpr int maxCells = 24;
    static constexpr int maxChannels = 2;
    static constexpr int controlInterval = 32;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (float) newSampleRate;
        // One-pole smoothing per control tick, ~25 ms time constant.
        const float ticksPerSecond = sampleRate / (float) controlInterval;
        smoothCoef = 1.0f - std::exp (-1.0f / (0.025f * ticksPerSecond));
        reset();
        firstTick = true;
    }

    void reset() noexcept
    {
        for (auto& ch : parallelCells) for (auto& c : ch) c.reset();
        for (auto& ch : serialCells)   for (auto& c : ch) c.reset();
        samplesUntilTick = 0;
    }

    void setSettings (const FieldSettings& s) noexcept { target = s; }

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

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = chans[ch][i];
                const float wet = processWet (applyDrive (dry), ch);
                float out = (dry + (wet - dry) * cur.mix) * outGain;

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
    float getCellFrequency (int i) const noexcept { return cellFreq[(size_t) i]; }
    int   getActiveCells() const noexcept { return activeCells; }

private:
    float applyDrive (float x) const noexcept
    {
        if (driveBlend <= 0.0f)
            return x;
        const float driven = softClip (x * driveGain) * driveMakeup;
        return x + (driven - x) * driveBlend;
    }

    float processWet (float x, int ch) noexcept
    {
        float par = 0.0f, ser = 0.0f;

        if (parallelWeight > 0.0f)
        {
            for (int c = 0; c < activeCells; ++c)
                par += parallelCells[(size_t) ch][(size_t) c].process (x) * cellGain[(size_t) c]
                     * (stereo ? pan[(size_t) ch][(size_t) c] : 1.0f);
            par *= parallelNorm;
        }

        if (serialWeight > 0.0f)
        {
            ser = x;
            for (int c = 0; c < activeCells; ++c)
            {
                const float y = serialCells[(size_t) ch][(size_t) c].process (ser);
                ser += (y - ser) * serialDepth[(size_t) c];
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
        glide (cur.driveDb, target.driveDb);
        glide (cur.mix, target.mix);
        glide (cur.outputDb, target.outputDb);
        cur.scale = target.scale;
        cur.density = target.density;
    }

    void controlTick() noexcept
    {
        smoothTowardsTarget();

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

        const float q = 0.5f * std::exp2 (cur.resonance * 7.0f);
        const float maxHz = 0.45f * sampleRate;
        const float stretch = cur.spread / 4.0f;
        const float serialBase = std::min (1.0f, 3.0f / (float) activeCells);

        for (int c = 0; c < activeCells; ++c)
        {
            float f;
            if (cur.scale == CellScale::Log)
            {
                const float pos = activeCells > 1 ? (float) c / (float) (activeCells - 1) - 0.5f : 0.0f;
                f = cur.centerHz * std::exp2 (pos * cur.spread);
            }
            else
            {
                f = cur.centerHz * cellRatio (cur.scale, c, stretch);
            }
            cellFreq[(size_t) c] = f;

            // Cells pushed above the usable range fade out instead of piling up at Nyquist.
            const float audible = f < maxHz ? 1.0f : 0.0f;
            const float octFromCenter = std::log2 (f / cur.centerHz);
            const float tiltDb = std::clamp (cur.tiltDbOct * octFromCenter, -24.0f, 24.0f);
            targetGain[(size_t) c] = audible * std::pow (10.0f, tiltDb / 20.0f);

            // Width: alternate cells lean left/right, and L/R cutoffs drift apart slightly.
            const float side = (c % 2 == 0) ? 1.0f : -1.0f;
            const float detuneOct = 0.1f * cur.width * side;
            const float p = cur.width * side;                // -1..1, +1 = left
            pan[0][(size_t) c] = std::min (1.0f, 1.0f + p);
            pan[1][(size_t) c] = std::min (1.0f, 1.0f - p);

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                const float chHz = std::clamp (f * std::exp2 (ch == 0 ? detuneOct : -detuneOct), 15.0f, maxHz);
                auto& pc = parallelCells[(size_t) ch][(size_t) c];
                auto& sc = serialCells[(size_t) ch][(size_t) c];
                pc.setCoefficients (chHz, q, sampleRate);
                sc.setCoefficients (chHz, q, sampleRate);
                pc.setMorph (cur.morph);
                sc.setMorph (cur.morph);
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

        // Level compensation: narrow (high-Q) bands pass less broadband energy, many cells add up.
        // A compromise between noise-like and tonal input; full energy compensation is phase 2.
        parallelNorm = 1.5f * std::pow (q, 0.3f) / std::sqrt ((float) activeCells);

        const float prevSerial = serialWeight;
        serialWeight = std::clamp (cur.combine, 0.0f, 1.0f);
        parallelWeight = 1.0f - serialWeight;
        if (prevSerial <= 0.0f && serialWeight > 0.0f)
            for (auto& ch : serialCells) for (auto& c : ch) c.reset();

        driveGain = std::pow (10.0f, cur.driveDb / 20.0f);
        driveMakeup = 1.0f / std::sqrt (driveGain);
        driveBlend = std::min (1.0f, cur.driveDb / 6.0f);
        outGain = std::pow (10.0f, cur.outputDb / 20.0f);
    }

    float sampleRate = 44100.0f;
    float smoothCoef = 1.0f;
    bool  firstTick = true;
    bool  stereo = true;
    int   samplesUntilTick = 0;

    FieldSettings target, cur;
    float logCenter = 0.0f;

    int activeCells = 0;
    std::array<std::array<FluxCell, maxCells>, maxChannels> parallelCells {}, serialCells {};
    std::array<float, maxCells> cellFreq {}, cellGain {}, targetGain {}, serialDepth {};
    std::array<std::array<float, maxCells>, maxChannels> pan {};

    float parallelNorm = 1.0f, parallelWeight = 1.0f, serialWeight = 0.0f;
    float driveGain = 1.0f, driveMakeup = 1.0f, driveBlend = 0.0f, outGain = 1.0f;
};

} // namespace flux

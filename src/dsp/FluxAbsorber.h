#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace flux
{

// An LFO's Flux share, absorbed by the living cells. Every cell hears the movement with its own
// delay (0–300 ms), its own depth (0.6–1.4×) and its own sluggishness (up to 120 ms), scaled by how
// alive it is (its life gain: 1 at Flux 0, 0 for a dead cell). So the cells follow the LFO loosely,
// like a crowd, instead of in lockstep. Each channel has its own traits; the right channel uses
// them only when Stereo life is independent.
// Runs once per control tick; no per-sample work.
template <int MaxCells, int MaxChannels = 2>
class FluxAbsorber
{
public:
    static constexpr float maxLagSec = 0.3f, maxSlugSec = 0.12f;
    using Weights = std::array<std::array<float, MaxCells>, MaxChannels>;

    // Allocates; call from prepareToPlay, not the audio thread.
    void prepare (float ticksPerSecond)
    {
        tickRate = ticksPerSecond;
        history.assign ((size_t) std::ceil (maxLagSec * ticksPerSecond) + 4, 0.0f);
        writePos = 0;
        uint32_t rng = 0x7F4A7C15u;
        auto next = [&rng] {   // uniform 0..1 (xorshift32)
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng >> 8) / 16777216.0f;
        };
        for (auto& ch : traits)
            for (auto& t : ch)
            {
                t.lagTicks = next() * maxLagSec * ticksPerSecond;
                t.depth = 0.6f + 0.8f * next();
                const float slug = next() * maxSlugSec;
                t.coef = slug > 1.0e-4f ? 1.0f - std::exp (-1.0f / (slug * ticksPerSecond)) : 1.0f;
            }
        for (auto& ch : values) ch.fill (0.0f);
        primed = false;
    }

    // Push this tick's source value, then compute every cell's value.
    // numPops: 1 = both channels use the left channel's traits (linked), 2 = their own.
    void process (float source, const Weights& weight, int numCells, int numPops) noexcept
    {
        if (history.empty())
            return;
        if (! primed)   // start settled: no movement rushing in from silence at load time
        {
            std::fill (history.begin(), history.end(), source);
            for (int ch = 0; ch < MaxChannels; ++ch)
                for (int c = 0; c < MaxCells; ++c)
                    values[(size_t) ch][(size_t) c] = source * traits[(size_t) (ch < numPops ? ch : 0)][(size_t) c].depth * weight[(size_t) ch][(size_t) c];
            primed = true;
        }
        writePos = (writePos + 1) % history.size();
        history[writePos] = source;

        numCells = std::clamp (numCells, 1, MaxCells);
        for (int ch = 0; ch < MaxChannels; ++ch)
            for (int c = 0; c < numCells; ++c)
            {
                const auto& t = traits[(size_t) (ch < numPops ? ch : 0)][(size_t) c];
                const float raw = read (t.lagTicks) * t.depth * weight[(size_t) ch][(size_t) c];
                auto& y = values[(size_t) ch][(size_t) c];
                y += (raw - y) * t.coef;
            }
    }

    float get (int cell, int ch) const noexcept { return values[(size_t) ch][(size_t) cell]; }
    float lagSeconds (int cell, int ch = 0) const noexcept { return traits[(size_t) ch][(size_t) cell].lagTicks / tickRate; }

private:
    // Linear interpolation, delay in ticks.
    float read (float delayTicks) const noexcept
    {
        delayTicks = std::clamp (delayTicks, 0.0f, (float) history.size() - 2.0f);
        const auto whole = (size_t) delayTicks;
        const float frac = delayTicks - (float) whole;
        const size_t n = history.size();
        const size_t i0 = (writePos + n - whole) % n;
        const size_t i1 = (i0 + n - 1) % n;
        return history[i0] + (history[i1] - history[i0]) * frac;
    }

    struct Traits { float lagTicks = 0.0f, depth = 1.0f, coef = 1.0f; };

    float tickRate = 1500.0f;
    std::vector<float> history;
    size_t writePos = 0;
    bool primed = false;
    std::array<std::array<Traits, MaxCells>, MaxChannels> traits {};
    std::array<std::array<float, MaxCells>, MaxChannels> values {};
};

} // namespace flux
